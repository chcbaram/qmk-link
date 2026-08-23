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


#ifndef TOTAL_EEPROM_BYTE_COUNT
#define TOTAL_EEPROM_BYTE_COUNT 16384
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


static uint8_t  eeprom_buf[TOTAL_EEPROM_BYTE_COUNT];
static uint32_t dirty_mask = 0;
static uint32_t dirty_time = 0;
static uint32_t flush_cnt  = 0;   /* 진단용 — 실제 소거 횟수 */
static uint32_t flush_time = 0;   /* 진단용 — 마지막 플러시에 걸린 us */
static bool     is_init    = false;




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
  uintptr_t offset = (uintptr_t)addr;

  if (offset >= sizeof(eeprom_buf)) return;
  if (offset + len > sizeof(eeprom_buf)) len = sizeof(eeprom_buf) - offset;

  memcpy(buf, &eeprom_buf[offset], len);
}

void eeprom_write_block(const void *buf, void *addr, size_t len)
{
  uintptr_t offset = (uintptr_t)addr;

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
