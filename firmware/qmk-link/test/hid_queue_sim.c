/*
 * usbd_hid.c 의 리포트 큐를 호스트에서 그대로 돌린다.
 *
 * 흉내내는 것은 TinyUSB 와 시간뿐이다. 큐 로직은 펌웨어 소스 원본이다.
 *   - 엔드포인트는 1ms 마다 하나씩 가져간다 (EP_KBD_INTERVAL)
 *   - tud_task() 한 번 = 50us
 *   - 매크로 · 탭댄스는 한 번의 keyboard_task() 안에서 리포트를 연달아 보낸다
 */
#include "usbd_hid.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define EP_INTERVAL_US   1000
#define TUD_TASK_US        50

static uint64_t now_us = 0;
static uint64_t ep_free_at[4] = {0,};
static bool     is_mounted = true;

// PC 가 실제로 받은 것
static uint8_t  seen[4096][8];
static int      seen_len[4096];
static int      seen_cnt = 0;

uint32_t millis(void) { return (uint32_t)(now_us / 1000); }

bool tud_hid_n_ready(uint8_t itf) { return is_mounted && now_us >= ep_free_at[itf]; }

bool tud_hid_n_report(uint8_t itf, uint8_t report_id, const void *report, uint16_t len)
{
  if (!tud_hid_n_ready(itf)) return false;

  if (itf == HID_ITF_KEYBOARD && seen_cnt < 4096)
  {
    memcpy(seen[seen_cnt], report, len > 8 ? 8 : len);
    seen_len[seen_cnt] = len;
    seen_cnt++;
  }
  (void)report_id;
  ep_free_at[itf] = now_us + EP_INTERVAL_US;   // 호스트가 가져갈 때까지 busy
  return true;
}

bool tud_hid_n_mouse_report(uint8_t itf, uint8_t id, uint8_t b, int8_t x, int8_t y, int8_t v, int8_t h)
{ (void)itf;(void)id;(void)b;(void)x;(void)y;(void)v;(void)h; return true; }
uint8_t tud_hid_n_get_protocol(uint8_t itf) { (void)itf; return 1; }
bool tud_mounted(void)   { return is_mounted; }
bool tud_suspended(void) { return false; }
void tud_task(void)      { now_us += TUD_TASK_US; }
const uint8_t *usbdHidGetReportDesc(uint8_t itf, uint16_t *p_len) { (void)itf; *p_len = 0; return NULL; }

void tud_mount_cb(void);

//-- 시나리오 재생
//
#define KC_A    0x04
#define KC_H    0x0B
#define KC_SPC  0x2C
#define M_LCTL  0x01
#define M_LALT  0x04

static void rpt(uint8_t *r, uint8_t mods, uint8_t key)
{
  memset(r, 0, 8);
  r[0] = mods;
  r[2] = key;
}

// 메인 루프 한 바퀴 (ap.c 의 cliLoopIdle)
static void main_loop_tick(void) { now_us += TUD_TASK_US; usbdHidUpdate(); }

static void reset_all(void)
{
  now_us = 0;
  for (int i=0; i<4; i++) ep_free_at[i] = 0;
  seen_cnt = 0;
  tud_mount_cb();          // 섀도 · 큐를 비운다
  now_us = 10000;          // 붙은 지 좀 지난 상태로 시작
}

// 한 번의 keyboard_task() 안에서 연달아 보낸다 — 사이에 메인 루프가 없다
static void burst(uint8_t (*seq)[8], int n)
{
  for (int i=0; i<n; i++) usbdHidSendKeyboard(seq[i]);
}

static void drain(int ms)
{
  uint64_t end = now_us + (uint64_t)ms * 1000;
  while (now_us < end) main_loop_tick();
}

static void dump(const char *tag, uint8_t (*seq)[8], int n)
{
  printf("  %s (%d): ", tag, n);
  for (int i=0; i<n; i++)
  {
    printf("[");
    if (seq[i][0] & M_LCTL) printf("C");
    if (seq[i][0] & M_LALT) printf("A");
    if (seq[i][2] == KC_A)   printf("a");
    if (seq[i][2] == KC_H)   printf("h");
    if (seq[i][2] == KC_SPC) printf("_");
    if (seq[i][0] == 0 && seq[i][2] == 0) printf("-");
    printf("]");
  }
  printf("\n");
}

static int run(const char *name, uint8_t (*seq)[8], int n, bool verbose)
{
  reset_all();
  burst(seq, n);
  drain(200);

  int ok = (seen_cnt == n);
  if (ok) for (int i=0; i<n; i++) if (memcmp(seen[i], seq[i], 8) != 0) { ok = 0; break; }

  printf("%-34s %s\n", name, ok ? "통과" : "★ 실패");
  if (verbose || !ok)
  {
    dump("보낸 것", seq, n);
    dump("PC 가 본 것", seen, seen_cnt);
    if (seen_cnt && memcmp(seen[seen_cnt-1], seq[n-1], 8) != 0)
      printf("  ★ 마지막 상태가 다르다 — 키가 눌린 채 남는다\n");
  }
  return ok;
}

int main(void)
{
  int fail = 0;
  static uint8_t seq[512][8];

  usbdHidInit();

  printf("=== %s ===\n\n", SIM_NAME);

  // 1. 매크로 Ctrl+Space (macOS 한영 전환)
  rpt(seq[0], M_LCTL, 0);
  rpt(seq[1], M_LCTL, KC_SPC);
  rpt(seq[2], M_LCTL, 0);
  rpt(seq[3], 0, 0);
  fail += !run("1. 매크로 Ctrl+Space", seq, 4, true);

  // 2. 매크로 안의 반복 키 "aa"
  rpt(seq[0], 0, KC_A);
  rpt(seq[1], 0, 0);
  rpt(seq[2], 0, KC_A);
  rpt(seq[3], 0, 0);
  fail += !run("2. 매크로 반복 키 aa", seq, 4, true);

  // 3. 탭댄스 on_tap = LCA(KC_H), 한 번 탭
  //    타임아웃에서 on_dance_finished(down) -> on_dance_reset(up) 이 한 루프에 몰린다
  rpt(seq[0], M_LCTL|M_LALT, 0);
  rpt(seq[1], M_LCTL|M_LALT, KC_H);
  rpt(seq[2], M_LCTL|M_LALT, 0);
  rpt(seq[3], 0, 0);
  fail += !run("3. 탭댄스 LCA(KC_H) 탭", seq, 4, true);

  // 4. 긴 매크로 — 40자 (down/up 80 리포트). 큐 16칸을 훨씬 넘긴다
  for (int i=0; i<40; i++) { rpt(seq[i*2], 0, KC_A); rpt(seq[i*2+1], 0, 0); }
  fail += !run("4. 긴 매크로 40자 (80 리포트)", seq, 80, false);

  // 5. 호스트가 안 가져갈 때 — 최종 상태만은 지켜지는가
  {
    reset_all();
    for (int i=0; i<40; i++) { rpt(seq[i*2], 0, KC_A); rpt(seq[i*2+1], 0, 0); }
    is_mounted = false;                       // 엔드포인트가 영영 안 빈다
    burst(seq, 80);
    is_mounted = true;
    ep_free_at[HID_ITF_KEYBOARD] = now_us;
    drain(200);
    bool last_ok = seen_cnt && memcmp(seen[seen_cnt-1], seq[79], 8) == 0;
    printf("%-34s %s\n", "5. 호스트가 멈춘 동안 80 리포트", last_ok ? "통과" : "★ 실패");
    printf("  PC 가 본 것 %d 개, 마지막이 '뗌' 인가: %s\n", seen_cnt, last_ok ? "예" : "아니오 (키 고착)");
    fail += !last_ok;
  }

  printf("\n결과: %s\n", fail ? "★ 실패 있음" : "전부 통과");
  return fail ? 1 : 0;
}
