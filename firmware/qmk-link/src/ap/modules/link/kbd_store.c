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

/* ───────────────────────── 선택 표 (kbd_store.h 주석 참고) ───────────────── */

#define SEL_ADDR        HW_FLASH_KBD_SEL_BEGIN
#define SEL_SIZE        HW_FLASH_KBD_SEL_SIZE
#define SEL_MAX         (SEL_SIZE / sizeof(kbd_sel_t))     /* 256 */
#define SEL_PER_PAGE    (HW_FLASH_PAGE_SIZE / sizeof(kbd_sel_t))   /* 16 */

static bool selMatch(const kbd_sel_t *p_rec, uint16_t vid, uint16_t pid, uint32_t hash)
{
  return (p_rec->magic == KBD_SEL_MAGIC &&
          p_rec->vid == vid && p_rec->pid == pid && p_rec->hash == hash);
}

/*
 * 표를 앞에서 끝까지 훑는다.
 *
 * ★ 마지막 일치가 정답이다 — 추가 기록이라 같은 키보드의 기록이 여럿 남는다.
 *   빈 자리(0xFF..)를 만나면 거기가 끝이고, 그 자리가 다음에 쓸 곳이다.
 *
 * p_free : 처음 만난 빈 자리의 번호 (없으면 SEL_MAX)
 */
static int selScan(uint16_t vid, uint16_t pid, uint32_t hash,
                   kbd_sel_t *p_out, uint16_t *p_free)
{
  uint8_t  page[HW_FLASH_PAGE_SIZE];
  int      found = -1;

  if (p_free != NULL) *p_free = SEL_MAX;

  for (uint16_t i = 0; i < SEL_MAX; i += SEL_PER_PAGE)
  {
    if (flashRead(SEL_ADDR + i * sizeof(kbd_sel_t), page, sizeof(page)) != true) break;

    for (uint16_t k = 0; k < SEL_PER_PAGE; k++)
    {
      const kbd_sel_t *p_rec = (const kbd_sel_t *)&page[k * sizeof(kbd_sel_t)];

      if (p_rec->magic != KBD_SEL_MAGIC)
      {
        /* 빈 자리를 만나면 그 뒤는 볼 것이 없다 */
        if (p_free != NULL && *p_free == SEL_MAX) *p_free = i + k;
        return found;
      }

      if (selMatch(p_rec, vid, pid, hash))
      {
        found = i + k;
        if (p_out != NULL) *p_out = *p_rec;
      }
    }
  }

  return found;
}

bool kbdSelGet(uint16_t vid, uint16_t pid, uint32_t hash, uint8_t *p_slot)
{
  kbd_sel_t rec;

  if (selScan(vid, pid, hash, &rec, NULL) < 0) return false;

  if (p_slot != NULL) *p_slot = rec.slot;
  return true;
}

/*
 * ★ 표가 꽉 차면 지우고 다시 쓴다 (compaction).
 *
 *   지금 담겨 있는 SLOT 들의 키보드에 대한 기록만 남긴다. 지워진 키보드의
 *   기록은 버린다 — 어차피 안 쓰인다. SLOT 이 16개뿐이라 살아남는 것도 16개
 *   이하다. 256칸을 다 쓰는 데 선택을 240번 넘게 바꿔야 하므로 드문 일이다.
 */
static bool selCompact(void)
{
  static kbd_sel_t keep[KBD_SLOT_MAX];      /* 8KB slot_buf 를 건드리지 않는다 */
  uint8_t          n = 0;

  for (uint8_t i = 0; i < KBD_SLOT_MAX; i++)
  {
    kbd_hdr_t hdr;
    kbd_sel_t rec;
    bool      dup = false;

    if (kbdStoreGetHeader(i, &hdr) != true) continue;
    if (selScan(hdr.vid, hdr.pid, hdr.hash, &rec, NULL) < 0) continue;

    for (uint8_t k = 0; k < n; k++)
      if (selMatch(&keep[k], rec.vid, rec.pid, rec.hash)) { dup = true; break; }

    if (dup == false && n < KBD_SLOT_MAX) keep[n++] = rec;
  }

  if (flashErase(SEL_ADDR, SEL_SIZE) != true) return false;
  if (n == 0) return true;

  return flashWrite(SEL_ADDR, (uint8_t *)keep, n * sizeof(kbd_sel_t));
}

/*
 * ★ 지운 자리에 16B 만 덧쓴다 — 섹터를 다시 지우지 않는다.
 *
 *   flashWrite() 는 소거하지 않고 페이지를 프로그램한다. 프로그램은 비트를
 *   1 -> 0 으로만 바꾸므로, 나머지가 0xFF 인 자리에 16B 를 넣는 것은 안전하고
 *   이미 쓴 이웃 기록은 같은 값으로 다시 프로그램돼 그대로 남는다.
 *   (한 페이지를 최대 16번 나눠 프로그램한다. W25Q 계열은 부분 프로그램
 *    횟수를 제한하지 않는다)
 */
bool kbdSelSet(uint16_t vid, uint16_t pid, uint32_t hash, uint8_t slot)
{
  kbd_sel_t rec;
  kbd_sel_t cur;
  uint16_t  free_idx = SEL_MAX;

  if (vid == 0 && pid == 0) return false;

  /* 이미 같은 값이면 표를 늘리지 않는다 */
  if (selScan(vid, pid, hash, &cur, &free_idx) >= 0 && cur.slot == slot) return true;

  memset(&rec, 0xFF, sizeof(rec));
  rec.vid     = vid;
  rec.pid     = pid;
  rec.hash    = hash;
  rec.slot    = slot;
  rec.profile = 0;
  rec.magic   = KBD_SEL_MAGIC;

  if (free_idx >= SEL_MAX)
  {
    if (selCompact() != true) return false;
    selScan(vid, pid, hash, NULL, &free_idx);
    if (free_idx >= SEL_MAX) return false;
  }

  return flashWrite(SEL_ADDR + free_idx * sizeof(kbd_sel_t),
                    (uint8_t *)&rec, sizeof(rec));
}

int kbdStoreGetSelected(void)
{
  uint8_t slot;

  if (kbdSelGet(cur_vid, cur_pid, cur_hash, &slot) != true) return -1;
  if (slot >= KBD_SLOT_MAX) return -1;      /* KBD_SEL_AUTO */

  return slot;
}

bool kbdStoreSelectSlot(uint8_t slot)
{
  if (cur_vid == 0 && cur_pid == 0) return false;

  if (slot != KBD_SEL_AUTO)
  {
    kbd_hdr_t hdr;

    /* 그 SLOT 이 정말 이 키보드의 것인지 본다. 아니면 기록이 죽은 값이 된다 */
    if (kbdStoreGetHeader(slot, &hdr) != true) return false;
    if (hdr.vid != cur_vid || hdr.pid != cur_pid) return false;
  }

  if (kbdSelSet(cur_vid, cur_pid, cur_hash, slot) != true) return false;

  kbdStoreReselect();
  return true;
}

/* ─────────────────────────────────────────────────────────────────────────── */

void kbdStoreSelect(uint16_t vid, uint16_t pid, uint32_t hash)
{
  int slot = -1;

  cur_vid  = vid;
  cur_pid  = pid;
  cur_hash = hash;

  if (vid != 0 || pid != 0)
  {
    uint8_t sel;

    /*
     * ★ 기록된 선택이 먼저다. 단 그것이 아직 유효할 때만이다.
     *
     *   고른 SLOT 을 지우거나 다른 키보드로 덮어쓸 수 있다. 그러면 기록은
     *   죽은 값이 되므로 조용히 첫 일치로 되돌아간다 — 기록이 없던 시절과
     *   같은 동작이라 이 기능 전에 담아 둔 보드도 그대로 돈다.
     */
    if (kbdSelGet(vid, pid, hash, &sel) == true && sel < KBD_SLOT_MAX)
    {
      kbd_hdr_t hdr;

      if (kbdStoreGetHeader(sel, &hdr) == true && hdr.vid == vid && hdr.pid == pid)
        slot = sel;
    }

    if (slot < 0) slot = kbdStoreFind(vid, pid, hash);
  }

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
    cliPrintf("지금 적용 : %d   (기록된 선택 %d, -1 = 자동)\n",
              kbdStoreGetActive(), kbdStoreGetSelected());

    for (uint8_t i=0; i<KBD_SLOT_MAX; i++)
    {
      if (kbdStoreGetHeader(i, &hdr) != true) continue;

      /* 같은 키보드를 담은 더 낮은 칸이 있으면 이 칸은 영영 안 쓰인다 */
      {
        int win = kbdStoreFind(hdr.vid, hdr.pid, hdr.hash);

        cliPrintf("  [%2d] %04X:%04X  hash %08X  %5d B  PID 0x%04X  %s%s\n",
                  i, hdr.vid, hdr.pid, (unsigned)hdr.hash, hdr.data_len,
                  LINK_PID_BASE + i, hdr.name,
                  (win >= 0 && win != i) ? "   ★ 가려짐" : "");
      }
    }
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "erase"))
  {
    int n = args->getData(1);

    cliPrintf("[%d] 지우기 : %s\n", n, kbdStoreErase(n) ? "OK" : "실패");
    ret = true;
  }

  /* 지금 꽂힌 키보드가 쓸 SLOT 을 고정한다 */
  if (args->argc == 2 && args->isStr(0, "sel"))
  {
    if (args->isStr(1, "auto"))
    {
      cliPrintf("자동 : %s\n", kbdStoreSelectSlot(KBD_SEL_AUTO) ? "OK" : "실패");
    }
    else
    {
      int n = args->getData(1);

      cliPrintf("[%d] 고정 : %s\n", n, kbdStoreSelectSlot((uint8_t)n) ? "OK" : "실패");
    }
    cliPrintf("지금 적용 : %d\n", kbdStoreGetActive());
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
    cliPrintf("kbd sel   slot|auto\n");
    cliPrintf("kbd test  slot\n");
  }
}
#endif
