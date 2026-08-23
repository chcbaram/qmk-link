/*
 * port/platforms/eeprom.c — QMK 의 EEPROM 을 내장 플래시로
 *
 * QMK 는 EEPROM 을 바이트 단위로 아무 때나 읽고 쓴다. NOR 플래시는 그렇게 못 쓴다
 * (섹터 단위 소거, 1->0 만 가능). 그래서 **RAM 섀도 + 지연 플러시** 로 간다.
 *
 *   읽기   RAM 섀도에서 바로. 플래시를 건드리지 않는다
 *   쓰기   RAM 섀도를 고치고 그 섹터를 dirty 로 표시만
 *   플러시 조용해진 뒤에 dirty 섹터를 하나씩 소거+기록
 *
 * ★ 지연 플러시가 핵심이다.
 *
 *   VIA 로 키맵을 바꾸면 바이트 쓰기가 수백 번 연달아 온다. 매번 플래시에 쓰면
 *   같은 섹터를 수백 번 지우게 되고(수명), 그동안 USB 가 멈춘다.
 *   조용해질 때까지 모았다가 쓰면 소거가 섹터당 1회다.
 *
 * ★ 한 번에 한 섹터만, 그리고 섹터 사이를 띄운다.
 *
 *   실측(qmk-link, W25Q16 / 120MHz):
 *
 *     소거 4KB : 약 30 ms   <- 그동안 core1 tuh_task 가 3~4 회밖에 못 돈다
 *     기록 4KB : 약 10 ms
 *     합계     : 약 41 ms
 *
 *   flash_safe_execute() 가 core1 을 세우기 때문에 이 시간은 **USB 호스트가
 *   통째로 멈추는 시간**이다. 41ms 를 6번 연속으로 때려도 키보드는 붙어 있었지만
 *   (mounted 1 / drop 0), 4섹터를 붙여 쓰면 164ms 가 되므로 EE_FLUSH_MS 만큼
 *   띄워서 나눠 쓴다. 사용자 입장에서는 어차피 저장이 1초 안에 끝난다.
 *
 * ★ 0xFF 페이지는 쓰지 않는다.
 *
 *   소거 직후 섹터는 전부 0xFF 다. 실제로 값이 들어찬 페이지만 기록하면
 *   대부분 비어 있는 EEPROM 에서 기록 10ms 가 거의 0 이 된다.
 */

#include "eeprom.h"
#include "flash.h"
#include "log.h"
#include <string.h>

/*
 * ★ 설정을 여기서도 읽어야 한다.
 *
 *   예전에는 include 가 없어서 TOTAL_EEPROM_BYTE_COUNT 가 아래 기본값(16384)으로
 *   잡히고 있었다. config.h 값과 우연히 같아서 아무도 몰랐다 — 그 값을 바꾸는
 *   순간 조용히 어긋난다. 키맵 프로파일은 MATRIX_ROWS · DYNAMIC_KEYMAP_* 도
 *   봐야 하므로 확실히 끌어온다.
 */
#include QMK_KEYMAP_CONFIG_H

#ifndef TOTAL_EEPROM_BYTE_COUNT
#error "config.h 가 안 잡혔다 — TOTAL_EEPROM_BYTE_COUNT 가 없다"
#endif

/*
 * ★ Vial 트리는 Vial 자리에 쓴다.
 *
 *   via 트리는 HW_FLASH_E2P_VIA_BEGIN 을 쓴다. 같은 보드에 두 펌웨어를 번갈아
 *   구워도 서로의 바이트를 읽지 않게 아예 다른 섹터에 둔다 (hw_def.h 의 배치).
 *
 *   ★ 이 줄을 via 에서 복사해 온 채로 두면 두 펌웨어가 같은 영역을 쓰면서
 *     조용히 섞인다. eeconfig 매직이 우연히 맞으면 초기화도 안 된다.
 */
#define EE_FLASH_BEGIN   HW_FLASH_E2P_VIAL_BEGIN
#define EE_FLASH_SIZE    HW_FLASH_E2P_VIAL_SIZE

#define EE_SECTOR_SIZE   HW_FLASH_SECTOR_SIZE
#define EE_SECTOR_CNT    (TOTAL_EEPROM_BYTE_COUNT / EE_SECTOR_SIZE)

/* 마지막 쓰기 뒤 이만큼 조용하면 플러시한다. 섹터 사이 간격이기도 하다. */
#define EE_FLUSH_MS      200


#if TOTAL_EEPROM_BYTE_COUNT > EE_FLASH_SIZE
#error "TOTAL_EEPROM_BYTE_COUNT 가 hw_def.h 의 EEPROM 영역보다 크다"
#endif
#if (TOTAL_EEPROM_BYTE_COUNT % EE_SECTOR_SIZE) != 0
#error "TOTAL_EEPROM_BYTE_COUNT 는 섹터 크기의 배수여야 한다"
#endif
/* dirty 표시가 uint32_t 비트마스크다. 32섹터(128KB)를 넘기면 조용히 잘린다 */
#if EE_SECTOR_CNT > 32
#error "EEPROM 이 32섹터를 넘는다 — dirty_mask 를 uint64_t 로 바꿔야 한다"
#endif


static uint8_t  eeprom_buf[TOTAL_EEPROM_BYTE_COUNT];
static uint32_t dirty_mask = 0;
static uint32_t dirty_time = 0;
static uint32_t flush_cnt  = 0;   /* 진단용 — 실제 소거 횟수 */
static uint32_t flush_time = 0;   /* 진단용 — 마지막 플러시에 걸린 us */
static bool     is_init    = false;

/* ────────────────────────── 키맵 프로파일 (09-3) ──────────────────────────
 *
 * ★ upstream 을 건드리지 않는다.
 *
 *   키맵 읽기/쓰기는 전부 eeprom_read_block / eeprom_write_block 을 지난다.
 *   그러니 여기서 **주소만 옮기면** 프로파일이 된다. nvm_dynamic_keymap.c 를
 *   복사해 올 필요도, QMK 를 패치할 필요도 없다.
 *
 *   프로파일 0 은 upstream 이 정한 자리 그대로다. 1~16 은 기본 영역 뒤에
 *   4096B 씩 늘어놓는다. 그래서 **09-2 까지 쓰던 키맵이 프로파일 0 이 된다.**
 *
 * ★ 걸치는 접근은 옮기지 않는다.
 *
 *   키맵 구간 안에 완전히 들어올 때만 옮긴다. 걸쳐 있으면 그대로 둔다 —
 *   경계를 넘는 덩어리 접근은 upstream 에 없고(via.c 가 키맵 크기로 자른다),
 *   반만 옮기면 조용히 섞이기 때문이다.
 */
#define KM_BEGIN     ((uintptr_t)DYNAMIC_KEYMAP_EEPROM_ADDR)
#define KM_SIZE      ((uintptr_t)EEPROM_PROFILE_KEYMAP_SIZE)
#define KM_EXTRA     ((uintptr_t)(DYNAMIC_KEYMAP_EEPROM_MAX_ADDR + 1))

static uint8_t km_profile   = 0;
static bool    km_need_fill = false;

/* 프로파일 p 의 키맵이 시작하는 주소 */
static uintptr_t kmBase(uint8_t p)
{
  return (p == 0) ? KM_BEGIN : (KM_EXTRA + (uintptr_t)(p - 1) * KM_SIZE);
}

static uintptr_t kmRemap(uintptr_t off, size_t len)
{
  if (km_profile == 0) return off;
  if (off < KM_BEGIN) return off;
  if (off + len > KM_BEGIN + KM_SIZE) return off;

  return kmBase(km_profile) + (off - KM_BEGIN);
}

static void kmDirty(uintptr_t off, size_t len)
{
  for (uintptr_t i = off; i < off + len; i += EE_SECTOR_SIZE)
    dirty_mask |= (1U << (i / EE_SECTOR_SIZE));

  dirty_mask |= (1U << ((off + len - 1) / EE_SECTOR_SIZE));
  dirty_time  = millis();
}

void eepromSetProfile(uint8_t p)
{
  if (p >= EEPROM_PROFILE_MAX) p = 0;
  km_profile = p;
}

uint8_t eepromGetProfile(void)
{
  return km_profile;
}

/* 지금 벌을 다른 벌로 베낀다. SLOT 을 새로 만들 때 쓴다 —
   빈 키맵으로 시작하면 키보드를 늘릴 때마다 설정을 처음부터 다시 해야 한다. */
void eepromProfileCopy(uint8_t to)
{
  uintptr_t src;
  uintptr_t dst;

  if (to >= EEPROM_PROFILE_MAX) return;
  if (to == km_profile) return;

  src = kmBase(km_profile);
  dst = kmBase(to);

  if (memcmp(&eeprom_buf[dst], &eeprom_buf[src], KM_SIZE) == 0) return;

  memcpy(&eeprom_buf[dst], &eeprom_buf[src], KM_SIZE);
  kmDirty(dst, KM_SIZE);
}

/* ★ 전 벌을 채운다.
 *
 *   dynamic_keymap_reset() 은 **지금 벌 하나만** 채운다. 그대로 두면 나머지가
 *   전부 0xFF 라, 다른 키보드를 꽂는 순간 키가 하나도 안 나간다.
 *   초기화 직후 한 번 지금 벌을 전부에 베껴 둔다. */
void eepromProfileFillAll(void)
{
  uint8_t keep = km_profile;

  km_need_fill = false;

  for (uint8_t p = 0; p < EEPROM_PROFILE_MAX; p++)
  {
    km_profile = keep;
    eepromProfileCopy(p);
  }
  km_profile = keep;

  logPrintf("[  ] eeprom 프로파일 %d벌 채움 (기준 %d)\r\n", EEPROM_PROFILE_MAX, keep);
}

/*
 * 프로파일이 채워져 있는지 보고, 아니면 채운다.
 *
 * ★ 표시를 EEPROM 안에 둔다.
 *
 *   "언제 채워야 하나" 가 갈래가 많다 — 처음 굽고 부팅했을 때, VIA 의
 *   Reset EEPROM 을 눌렀을 때, EEPROM 자리를 옮겼을 때. 각각을 코드로 쫓으면
 *   반드시 하나를 빠뜨린다. 매직 하나를 두고 **없으면 채운다** 로 통일한다.
 *
 *   512 는 eeconfig 가 안 쓰는 자리다 (거긴 100바이트 남짓이고 키맵은 1024 부터).
 */
#define KM_MAGIC_ADDR   512
#define KM_MAGIC        0x314D4B50UL   /* "PKM1" */

void eepromProfileEnsure(void)
{
  uint32_t magic;

  if (is_init != true) return;

  memcpy(&magic, &eeprom_buf[KM_MAGIC_ADDR], sizeof(magic));
  if (magic == KM_MAGIC && km_need_fill == false) return;

  eepromProfileFillAll();

  magic = KM_MAGIC;
  memcpy(&eeprom_buf[KM_MAGIC_ADDR], &magic, sizeof(magic));
  kmDirty(KM_MAGIC_ADDR, sizeof(magic));
}





/* dirty 섹터 하나를 실제로 기록한다. 여기서만 플래시를 건드린다. */
static bool eepromFlushOne(void)
{
  uint32_t s;
  uint32_t addr;
  uint8_t *p_sector;
  uint32_t exe_time;

  if (dirty_mask == 0) return false;

  s        = (uint32_t)__builtin_ctz(dirty_mask);
  addr     = EE_FLASH_BEGIN + s * EE_SECTOR_SIZE;
  p_sector = &eeprom_buf[s * EE_SECTOR_SIZE];

  /* 실패해도 dirty 를 내린다. 안 그러면 매 루프마다 재시도하며 USB 를 굶긴다. */
  dirty_mask &= ~(1U << s);
  dirty_time  = millis();

  exe_time = micros();

  if (flashErase(addr, EE_SECTOR_SIZE) != true)
  {
    logPrintf("[E_] eeprom 소거 실패 0x%06X\r\n", (unsigned)addr);
    return false;
  }

  /*
   * 소거 직후는 전부 0xFF 다. 값이 있는 페이지만 쓴다.
   *
   * ★ 연속된 페이지는 한 번에 묶는다.
   *
   *   flashWrite() 한 번이 flash_safe_execute() 한 번이고, 그때마다 core1 을
   *   세웠다 푸는 비용이 붙는다. 4KB 를 페이지마다 부르면 16회가 되어 실측 49ms,
   *   묶으면 1회로 41ms 다. 대부분의 섹터는 어차피 한 덩어리다.
   */
  {
    uint32_t run_begin = EE_SECTOR_SIZE;   /* 진행 중인 덩어리의 시작. 없으면 SIZE */

    for (uint32_t i=0; i<=EE_SECTOR_SIZE; i+=HW_FLASH_PAGE_SIZE)
    {
      bool is_blank = true;

      if (i < EE_SECTOR_SIZE)
      {
        for (uint32_t j=0; j<HW_FLASH_PAGE_SIZE; j++)
        {
          if (p_sector[i + j] != 0xFF) { is_blank = false; break; }
        }
      }

      if (is_blank == false)
      {
        if (run_begin == EE_SECTOR_SIZE) run_begin = i;
        continue;
      }

      /* 빈 페이지를 만났거나 섹터 끝이다 — 여기까지의 덩어리를 쓴다 */
      if (run_begin != EE_SECTOR_SIZE)
      {
        if (flashWrite(addr + run_begin, &p_sector[run_begin], i - run_begin) != true)
        {
          logPrintf("[E_] eeprom 기록 실패 0x%06X\r\n", (unsigned)(addr + run_begin));
          return false;
        }
        run_begin = EE_SECTOR_SIZE;
      }
    }
  }

  flush_time = micros() - exe_time;
  flush_cnt++;

  return true;
}


void eeprom_driver_init(void)
{
  /*
   * 지운 적 없는 플래시는 전부 0xFF 다. QMK 의 eeconfig 가 매직 워드를 보고 스스로
   * 초기화하므로 여기서 판단하지 않는다 — 읽어만 준다.
   */
  flashRead(EE_FLASH_BEGIN, eeprom_buf, TOTAL_EEPROM_BYTE_COUNT);
  dirty_mask = 0;
  is_init    = true;
}

void eeprom_driver_format(bool erase)
{
  (void)erase;
  eeprom_driver_erase();
}

void eeprom_driver_erase(void)
{
  /* 다 지웠으니 프로파일도 다시 채워야 한다. QMK 가 기본 키맵을 만든 뒤에 한다 */
  km_need_fill = true;

  memset(eeprom_buf, 0xFF, sizeof(eeprom_buf));

  dirty_mask = (1U << EE_SECTOR_CNT) - 1;
  dirty_time = millis();
}

/* 남은 것을 전부 지금 쓴다. 리셋·부트로더 진입 직전에 부른다. */
void eeprom_flush(void)
{
  while (dirty_mask) eepromFlushOne();
}

/*
 * ★ 계속 불려야 한다 — qmk.c 의 qmkUpdate() 가 부른다.
 *
 *   여기서만 플래시에 쓴다. 이걸 안 부르면 전원을 내리는 순간 다 날아간다.
 */
void eeprom_task(void)
{
  if (dirty_mask == 0) return;
  if ((millis() - dirty_time) < EE_FLUSH_MS) return;

  eepromFlushOne();
}

bool     eepromIsInit(void)        { return is_init; }
uint32_t eepromGetBase(void)       { return EE_FLASH_BEGIN; }
uint32_t eepromGetFlushCount(void) { return flush_cnt; }
uint32_t eepromGetFlushTime(void)  { return flush_time; }
uint32_t eepromGetDirtyMask(void)  { return dirty_mask; }


void eeprom_read_block(void *buf, const void *addr, size_t len)
{
  uintptr_t offset = kmRemap((uintptr_t)addr, len);

  if (offset >= sizeof(eeprom_buf)) return;
  if (offset + len > sizeof(eeprom_buf)) len = sizeof(eeprom_buf) - offset;

  memcpy(buf, &eeprom_buf[offset], len);
}

void eeprom_write_block(const void *buf, void *addr, size_t len)
{
  uintptr_t offset = kmRemap((uintptr_t)addr, len);

  if (offset >= sizeof(eeprom_buf)) return;
  if (offset + len > sizeof(eeprom_buf)) len = sizeof(eeprom_buf) - offset;

  if (memcmp(&eeprom_buf[offset], buf, len) == 0) return;   /* 안 바뀌면 dirty 도 없다 */

  memcpy(&eeprom_buf[offset], buf, len);

  /* 플래시에 바로 쓰지 않는다. 섹터를 표시해두고 조용해지면 eeprom_task() 가 쓴다 */
  for (uintptr_t i=offset; i<offset+len; i+=EE_SECTOR_SIZE)
  {
    dirty_mask |= (1U << (i / EE_SECTOR_SIZE));
  }
  dirty_mask |= (1U << ((offset + len - 1) / EE_SECTOR_SIZE));
  dirty_time  = millis();
}

uint8_t eeprom_read_byte(const uint8_t *addr)
{
  uint8_t data = 0xFF;
  eeprom_read_block(&data, addr, 1);
  return data;
}

void eeprom_write_byte(uint8_t *addr, uint8_t value)
{
  eeprom_write_block(&value, addr, 1);
}

uint16_t eeprom_read_word(const uint16_t *addr)
{
  uint16_t data = 0xFFFF;
  eeprom_read_block(&data, addr, 2);
  return data;
}

void eeprom_write_word(uint16_t *addr, uint16_t value)
{
  eeprom_write_block(&value, addr, 2);
}

uint32_t eeprom_read_dword(const uint32_t *addr)
{
  uint32_t data = 0xFFFFFFFF;
  eeprom_read_block(&data, addr, 4);
  return data;
}

void eeprom_write_dword(uint32_t *addr, uint32_t value)
{
  eeprom_write_block(&value, addr, 4);
}

/* update 계열은 write 가 이미 비교하고 들어간다 */
void eeprom_update_block(const void *buf, void *addr, size_t len) { eeprom_write_block(buf, addr, len); }
void eeprom_update_byte(uint8_t *addr, uint8_t value)             { eeprom_write_byte(addr, value); }
void eeprom_update_word(uint16_t *addr, uint16_t value)           { eeprom_write_word(addr, value); }
void eeprom_update_dword(uint32_t *addr, uint32_t value)          { eeprom_write_dword(addr, value); }
