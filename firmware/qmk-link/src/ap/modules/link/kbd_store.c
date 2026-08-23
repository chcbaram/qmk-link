#include "kbd_store.h"
#include "flash.h"
#include "cli.h"
#include "log.h"
#include "link_cmd.h"
#include <string.h>


#define SLOT_SIZE       HW_FLASH_KBD_SLOT_SIZE
#define SLOT_ADDR(n)    (HW_FLASH_KBD_BEGIN + (uint32_t)(n) * SLOT_SIZE)
#define DATA_OFFSET     sizeof(kbd_hdr_t)


#if CLI_USE(AP_KBD)
static void cliCmd(cli_args_t *args);
#endif

/*
 * 굽기 전에 한 칸을 통째로 조립한다.
 *
 * ★ 플래시는 페이지(256B) 단위로만 쓴다. 머리말 48B 와 데이터를 따로 쓰면
 *   경계가 어긋난다. RAM 에 한 칸을 만들어 한 번에 굽는 쪽이 단순하고 안전하다.
 *   8KB 는 RAM 340KB 가 남는 지금 부담이 아니다.
 */
static uint8_t slot_buf[SLOT_SIZE];

static bool      is_init    = false;
static kbd_hdr_t stage_hdr;
static bool      stage_open = false;
static int       active_slot = -1;
static uint16_t  cur_vid = 0;
static uint16_t  cur_pid = 0;
static uint32_t  cur_hash = 0;




bool kbdStoreInit(void)
{
  is_init = true;

#if CLI_USE(AP_KBD)
  cliAdd("kbd", cliCmd);
#endif

  return is_init;
}

uint16_t kbdStoreDataMax(void)
{
  return (uint16_t)(SLOT_SIZE - DATA_OFFSET);
}

bool kbdStoreGetHeader(uint8_t slot, kbd_hdr_t *p_hdr)
{
  if (slot >= KBD_SLOT_MAX) return false;
  if (flashRead(SLOT_ADDR(slot), (uint8_t *)p_hdr, sizeof(kbd_hdr_t)) != true) return false;

  if (p_hdr->magic != KBD_MAGIC) return false;
  if (p_hdr->version != KBD_VERSION) return false;
  if (p_hdr->data_len > kbdStoreDataMax()) return false;

  p_hdr->name[KBD_NAME_MAX-1] = 0;
  return true;
}

uint8_t kbdStoreUsedCount(void)
{
  kbd_hdr_t hdr;
  uint8_t   n = 0;

  for (uint8_t i=0; i<KBD_SLOT_MAX; i++)
  {
    if (kbdStoreGetHeader(i, &hdr) == true) n++;
  }
  return n;
}

int kbdStoreFind(uint16_t vid, uint16_t pid, uint32_t hash)
{
  kbd_hdr_t hdr;
  int       loose = -1;

  for (uint8_t i=0; i<KBD_SLOT_MAX; i++)
  {
    if (kbdStoreGetHeader(i, &hdr) != true) continue;
    if (hdr.vid != vid || hdr.pid != pid) continue;

    /* hash 까지 맞으면 그것이 정답이다 */
    if (hash != 0 && hdr.hash == hash) return i;

    /* vid/pid 만 맞는 것은 후보로 둔다 */
    if (loose < 0) loose = i;
  }

  return loose;
}

bool kbdStoreRead(uint8_t slot, uint32_t offset, uint8_t *p_data, uint16_t length)
{
  if (slot >= KBD_SLOT_MAX) return false;
  if (offset + length > kbdStoreDataMax()) return false;

  return flashRead(SLOT_ADDR(slot) + DATA_OFFSET + offset, p_data, length);
}

bool kbdStoreWrite(uint8_t slot, const kbd_hdr_t *p_hdr, const uint8_t *p_data)
{
  kbd_hdr_t *p_dst = (kbd_hdr_t *)slot_buf;

  if (slot >= KBD_SLOT_MAX) return false;
  if (p_hdr->data_len > kbdStoreDataMax()) return false;

  /* 안 쓰는 자리는 0xFF 로 둔다 — 소거 상태와 같아 굽는 양이 줄지는 않지만
     되읽을 때 쓰레기가 안 보인다 */
  memset(slot_buf, 0xFF, sizeof(slot_buf));

  memcpy(p_dst, p_hdr, sizeof(kbd_hdr_t));
  p_dst->magic   = KBD_MAGIC;
  p_dst->version = KBD_VERSION;

  memcpy(&slot_buf[DATA_OFFSET], p_data, p_hdr->data_len);

  if (flashErase(SLOT_ADDR(slot), SLOT_SIZE) != true) return false;
  if (flashWrite(SLOT_ADDR(slot), slot_buf, SLOT_SIZE) != true) return false;

  return true;
}

bool kbdStoreErase(uint8_t slot)
{
  if (slot >= KBD_SLOT_MAX) return false;

  return flashErase(SLOT_ADDR(slot), SLOT_SIZE);
}

void kbdStoreSelect(uint16_t vid, uint16_t pid, uint32_t hash)
{
  int slot;

  cur_vid  = vid;
  cur_pid  = pid;
  cur_hash = hash;

  slot = (vid == 0 && pid == 0) ? -1 : kbdStoreFind(vid, pid, hash);

  if (slot == active_slot) return;

  active_slot = slot;

  if (slot >= 0)
  {
    kbd_hdr_t hdr;

    if (kbdStoreGetHeader(slot, &hdr) == true)
    {
      logPrintf("[  ] kbd 레이아웃 [%d] %04X:%04X %s\r\n", slot, hdr.vid, hdr.pid, hdr.name);
    }
  }
  else
  {
    logPrintf("[  ] kbd 저장된 레이아웃 없음 — 기본 배열\r\n");
  }
}

int kbdStoreGetActive(void)
{
  return active_slot;
}

void kbdStoreReselect(void)
{
  active_slot = -1;                       /* 강제로 다시 찾게 한다 */
  kbdStoreSelect(cur_vid, cur_pid, cur_hash);
}

void kbdStoreStageBegin(const kbd_hdr_t *p_hdr)
{
  memset(slot_buf, 0xFF, sizeof(slot_buf));
  stage_hdr  = *p_hdr;
  stage_open = true;
}

bool kbdStoreStageData(uint16_t offset, const uint8_t *p_data, uint8_t length)
{
  if (stage_open != true) return false;
  if ((uint32_t)offset + length > kbdStoreDataMax()) return false;

  memcpy(&slot_buf[DATA_OFFSET + offset], p_data, length);
  return true;
}

bool kbdStoreStageCommit(uint8_t slot)
{
  kbd_hdr_t *p_dst = (kbd_hdr_t *)slot_buf;

  if (stage_open != true) return false;
  if (slot >= KBD_SLOT_MAX) return false;
  if (stage_hdr.data_len > kbdStoreDataMax()) return false;

  stage_open = false;

  memcpy(p_dst, &stage_hdr, sizeof(kbd_hdr_t));
  p_dst->magic   = KBD_MAGIC;
  p_dst->version = KBD_VERSION;

  if (flashErase(SLOT_ADDR(slot), SLOT_SIZE) != true) return false;
  if (flashWrite(SLOT_ADDR(slot), slot_buf, SLOT_SIZE) != true) return false;

  return true;
}

uint32_t kbdStoreHash(const uint8_t *p_data, uint16_t length)
{
  uint32_t h = 2166136261UL;      /* FNV-1a 32 */

  for (uint16_t i=0; i<length; i++)
  {
    h ^= p_data[i];
    h *= 16777619UL;
  }
  return h ? h : 1;               /* 0 은 "안 씀" 이라 피한다 */
}



#if CLI_USE(AP_KBD)
void cliCmd(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 0 || (args->argc == 1 && args->isStr(0, "info")))
  {
    kbd_hdr_t hdr;

    cliPrintf("저장소   : 0x%06X  %d칸 x %d KB\n",
              (unsigned)HW_FLASH_KBD_BEGIN, KBD_SLOT_MAX, (int)(SLOT_SIZE/1024));
    cliPrintf("칸당 데이터 : %d B\n", kbdStoreDataMax());
    cliPrintf("쓴 칸    : %d / %d\n", kbdStoreUsedCount(), KBD_SLOT_MAX);

    for (uint8_t i=0; i<KBD_SLOT_MAX; i++)
    {
      if (kbdStoreGetHeader(i, &hdr) != true) continue;

      cliPrintf("  [%2d] %04X:%04X  hash %08X  %5d B  PID 0x%04X  %s\n",
                i, hdr.vid, hdr.pid, (unsigned)hdr.hash, hdr.data_len,
                LINK_PID_BASE + i, hdr.name);
    }
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "erase"))
  {
    int n = args->getData(1);

    cliPrintf("[%d] 지우기 : %s\n", n, kbdStoreErase(n) ? "OK" : "실패");
    ret = true;
  }

  /* 저장 · 읽기 경로가 맞는지 보는 용도 */
  if (args->argc == 2 && args->isStr(0, "test"))
  {
    int       n = args->getData(1);
    kbd_hdr_t hdr;
    uint8_t   data[64];
    uint8_t   rd[64];

    memset(&hdr, 0, sizeof(hdr));
    hdr.vid = 0x04FE; hdr.pid = 0x0006;
    hdr.data_len = sizeof(data);
    hdr.hash = kbdStoreHash((const uint8_t *)"test", 4);
    snprintf(hdr.name, KBD_NAME_MAX, "test-%d", n);

    for (int i=0; i<(int)sizeof(data); i++) data[i] = (uint8_t)(i ^ n);

    cliPrintf("쓰기 : %s\n", kbdStoreWrite(n, &hdr, data) ? "OK" : "실패");

    memset(rd, 0, sizeof(rd));
    cliPrintf("읽기 : %s\n", kbdStoreRead(n, 0, rd, sizeof(rd)) ? "OK" : "실패");
    cliPrintf("일치 : %s\n", memcmp(data, rd, sizeof(data)) == 0 ? "예" : "★아니오★");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("kbd info\n");
    cliPrintf("kbd erase slot\n");
    cliPrintf("kbd test  slot\n");
  }
}
#endif
