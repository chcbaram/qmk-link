#include "usbd_hid.h"

#ifdef _USE_HW_USB
#include "tusb.h"
#ifdef QMK_ENABLE
#include "usb_device_state.h"
#endif






static uint8_t hid_led = 0;




bool usbdHidInit(void)
{
  hid_led = 0;
  return true;
}

bool usbdHidIsReady(uint8_t itf)
{
  return tud_hid_n_ready(itf);
}

// ★ 전송 정책 — "바뀔 때만 싣는다, 못 보내면 쌓아 둔다"
//
//   (1) QMK 는 같은 리포트를 여러 번 보낼 수 있다 (레이어 처리 중간 상태 등).
//       그때마다 전송을 걸면 옛 리포트가 먼저 나가느라 지연이 오히려 늘어난다.
//       섀도와 비교해 같으면 아무것도 하지 않는다. (wish-he 의 driver_usb.c 관례)
//
//   ★ 섀도는 "마지막으로 **큐에 넣은**" 리포트다. "마지막으로 나간 것" 이 아니다.
//
//     나간 것과 비교하면 이렇게 깨진다 —
//       A (빈)  전송,   섀도 = 빈
//       B  X    큐에 대기
//       C (빈)  섀도와 같다 → 버림 ★
//       → 뒤늦게 B 가 나가고 끝. PC 는 X 를 **누른 채로** 남는다.
//     매크로 안의 같은 키 반복(`aa`)도 두 번째가 통째로 사라진다.
//
//   (2) 못 보낸 리포트는 버리지 않는다.
//
//       엔드포인트가 바쁘면(raw HID 가 몰릴 때 실제로 생긴다 — 실측 not ready 13)
//       예전에는 그대로 버렸다. QMK 는 재시도하지 않으므로 그 키 이벤트는 영영
//       사라진다. 눌림이 유실되면 안 눌린 것이 되고, 뗌이 유실되면 PC 에 키가
//       눌린 채로 남는다.
//
//   ★ 보류함이 **한 칸이면 모자란다.** 큐여야 한다.
//
//     엔드포인트 interval 은 1ms 다. 한 번의 keyboard_task() 안에서 보낸 두 번째
//     리포트부터는 거의 항상 busy 다. 그런데 매크로 · 탭댄스는 한 task 안에서
//     리포트를 연달아 보낸다 (TAP_CODE_DELAY 기본 0).
//
//       매크로 Ctrl+Space → [Ctrl] [Ctrl+Space] [Ctrl] [빈]
//       한 칸이면 뒤엣것이 앞엣것을 덮어써서 PC 는 Ctrl 누름/뗌만 본다.
//       Space 는 한 번도 안 나간다.
//
//     일반 키보드의 QMK(ChibiOS)는 엔드포인트가 빌 때까지 기다린다. 우리는
//     기다리는 대신 쌓아 두고 usbdHidUpdate() 가 하나씩 꺼내 보낸다.
#define KBD_QUEUE_MAX       USBD_HID_KBD_QUEUE_MAX
#define HID_QUEUE_WAIT_MS   10        // 큐가 꽉 찼을 때 기다리는 한계

static uint8_t  kbd_queue[KBD_QUEUE_MAX][8];
static uint32_t kbd_wr = 0;
static uint32_t kbd_rd = 0;

static uint8_t  kbd_shadow[8];
static bool     kbd_shadow_valid = false;

// 진단용 — PC 로 나가는 키보드 리포트가 어디서 막히는지 센다.
static volatile uint32_t kbd_try_cnt  = 0;   // 호출
static volatile uint32_t kbd_same_cnt = 0;   // 섀도와 같아 건너뜀
static volatile uint32_t kbd_busy_cnt = 0;   // tud_hid_n_ready 가 false
static volatile uint32_t kbd_sent_cnt = 0;   // tud_hid_n_report 성공
static volatile uint32_t kbd_fail_cnt = 0;   // tud_hid_n_report 실패
static volatile uint32_t kbd_retry_cnt = 0;  // 큐에 쌓아 둔 것을 나중에 보낸 횟수
static volatile uint32_t kbd_wait_cnt = 0;   // 큐가 꽉 차 기다린 횟수
static volatile uint32_t kbd_over_cnt = 0;   // 기다려도 안 빠져 마지막 칸을 덮음
static volatile uint8_t  kbd_depth_max = 0;

// IF1(Extra) 큐 — NKRO · 마우스 · 컨슈머 · 시스템이 같이 탄다.
//
// ★ 이쪽도 큐가 필요하다. 예전에는 busy 면 **그냥 버렸다.**
//   macOS 처럼 report protocol 인 호스트에서 NKRO 를 켜면 키 리포트가 통째로
//   이 길로 나간다. 뗌이 버려지면 키가 눌린 채 남는다.
//
// 칸 크기는 가장 큰 리포트에 맞춘다 — report_nkro_t 가 32 B 이고
// (report_id + mods + bits[30]), ID 를 떼고 나가므로 31 B 면 된다.
#define EXTRA_QUEUE_MAX     USBD_HID_EXTRA_QUEUE_MAX
#define EXTRA_DATA_MAX      HID_NKRO_REPORT_LEN   /* 31 */

typedef struct
{
  uint8_t id;
  uint8_t len;
  uint8_t data[EXTRA_DATA_MAX];
} extra_item_t;

static extra_item_t extra_queue[EXTRA_QUEUE_MAX];
static uint32_t     extra_wr = 0;
static uint32_t     extra_rd = 0;

static volatile uint32_t extra_try_cnt  = 0;
static volatile uint32_t extra_sent_cnt = 0;
static volatile uint32_t extra_busy_cnt = 0;
static volatile uint32_t extra_fail_cnt = 0;
static volatile uint32_t extra_wait_cnt = 0;
static volatile uint32_t extra_over_cnt = 0;
static volatile uint8_t  extra_depth_max = 0;

// raw HID 수신 큐. 호스트 -> 우리 방향만 큐를 탄다 (usbd_hid.h 주석 참고).
#define HID_RAW_QUEUE_MAX   8
static uint8_t           raw_queue[HID_RAW_QUEUE_MAX][HID_RAW_REPORT_LEN];
static volatile uint32_t raw_rx_count   = 0;
static volatile uint32_t raw_rd_count   = 0;
static volatile uint32_t raw_drop_count = 0;

/*
 * 큐에서 하나 꺼내 내보낸다. 메인 루프가 계속 부른다 (usbdHidUpdate).
 *
 * ★ 한 번에 하나다.
 *
 *   tud_hid_n_report() 를 걸면 호스트가 가져갈 때까지(interval 1ms) 엔드포인트가
 *   busy 다. 여기서 더 돌아 봐야 not ready 만 센다.
 */
static bool usbdHidFlushKeyboard(void)
{
  bool ret;

  if (kbd_wr == kbd_rd) return true;

  if (tud_hid_n_ready(HID_ITF_KEYBOARD) != true)
  {
    kbd_busy_cnt++;
    return false;
  }

  // 리포트 ID 를 안 쓰므로 0 을 넘긴다. 8바이트가 그대로 나간다.
  ret = tud_hid_n_report(HID_ITF_KEYBOARD, 0, kbd_queue[kbd_rd % KBD_QUEUE_MAX], 8);

  if (ret != true)
  {
    kbd_fail_cnt++;
    return false;
  }

  kbd_rd++;
  kbd_sent_cnt++;

  return true;
}

static bool usbdHidFlushExtra(void)
{
  extra_item_t *p_item;
  bool          ret;

  if (extra_wr == extra_rd) return true;

  if (tud_hid_n_ready(HID_ITF_EXTRA) != true)
  {
    extra_busy_cnt++;
    return false;
  }

  p_item = &extra_queue[extra_rd % EXTRA_QUEUE_MAX];

  ret = tud_hid_n_report(HID_ITF_EXTRA, p_item->id, p_item->data, p_item->len);

  if (ret != true)
  {
    extra_fail_cnt++;
    return false;
  }

  extra_rd++;
  extra_sent_cnt++;

  return true;
}

/*
 * 큐가 꽉 찼을 때 잠깐 기다린다.
 *
 * ★ 여기서 delay() 를 쓰면 안 된다.
 *
 *   bsp.c 의 delay() 는 cliLoopIdle() 을 돌리고 그 안에 qmkUpdate() 가 있다.
 *   우리는 지금 그 keyboard_task() 안에서 불려 온 것이다. tud_task() 만 돌린다.
 *   (usbdHidSendRaw() 가 같은 이유로 같은 모양이다)
 *
 * ★ 붙어 있지 않으면 기다리지 않는다.
 *
 *   호스트가 없으면 ready 가 영영 false 다. 리포트마다 10ms 씩 까먹으면
 *   키보드가 멈춘 것처럼 보인다.
 */
static void usbdHidWaitRoom(bool (*flush_fn)(void), bool (*is_full_fn)(void))
{
  uint32_t pre_time;

  if (tud_mounted() != true || tud_suspended() == true) return;

  pre_time = millis();
  while (is_full_fn() == true)
  {
    if (millis() - pre_time >= HID_QUEUE_WAIT_MS) break;

    tud_task();
    flush_fn();
  }
}

static bool usbdHidKbdIsFull(void)
{
  return (kbd_wr - kbd_rd) >= KBD_QUEUE_MAX;
}

static bool usbdHidExtraIsFull(void)
{
  return (extra_wr - extra_rd) >= EXTRA_QUEUE_MAX;
}

void usbdHidUpdate(void)
{
  if (kbd_wr != kbd_rd)
  {
    if (usbdHidFlushKeyboard() == true) kbd_retry_cnt++;
  }

  usbdHidFlushExtra();
}

bool usbdHidSendKeyboard(const uint8_t *p_report)
{
  uint8_t depth;

  kbd_try_cnt++;

  if (kbd_shadow_valid && memcmp(kbd_shadow, p_report, 8) == 0)
  {
    kbd_same_cnt++;
    return true;
  }

  // ★ 섀도는 큐에 넣은 시점에 갱신한다 (위 ★ 주석).
  memcpy(kbd_shadow, p_report, 8);
  kbd_shadow_valid = true;

  if (usbdHidKbdIsFull() == true)
  {
    kbd_wait_cnt++;
    usbdHidWaitRoom(usbdHidFlushKeyboard, usbdHidKbdIsFull);
  }

  if (usbdHidKbdIsFull() == true)
  {
    /*
     * ★ 넘치면 **마지막 칸을 덮는다.** 가장 오래된 것을 버리지 않는다.
     *
     *   중간 상태는 잃어도 되지만 최종 상태는 반드시 PC 로 가야 한다.
     *   뗌을 잃으면 키가 눌린 채로 남는다.
     */
    kbd_over_cnt++;
    memcpy(kbd_queue[(kbd_wr - 1) % KBD_QUEUE_MAX], p_report, 8);
    return false;
  }

  memcpy(kbd_queue[kbd_wr % KBD_QUEUE_MAX], p_report, 8);
  kbd_wr++;

  depth = (uint8_t)(kbd_wr - kbd_rd);
  if (depth > kbd_depth_max) kbd_depth_max = depth;

  // 큐가 비어 있었고 엔드포인트도 비어 있으면 곧바로 나간다.
  return usbdHidFlushKeyboard();
}

void usbdHidGetKbdStat(usbd_hid_kbd_stat_t *p_stat)
{
  p_stat->try_cnt   = kbd_try_cnt;
  p_stat->same_cnt  = kbd_same_cnt;
  p_stat->busy_cnt  = kbd_busy_cnt;
  p_stat->sent_cnt  = kbd_sent_cnt;
  p_stat->fail_cnt  = kbd_fail_cnt;
  p_stat->retry_cnt = kbd_retry_cnt;
  p_stat->wait_cnt  = kbd_wait_cnt;
  p_stat->over_cnt  = kbd_over_cnt;
  p_stat->depth     = (uint8_t)(kbd_wr - kbd_rd);
  p_stat->depth_max = kbd_depth_max;
  p_stat->is_ready  = tud_hid_n_ready(HID_ITF_KEYBOARD);
  p_stat->is_mount  = tud_mounted();
  p_stat->is_susp   = tud_suspended();
}

void usbdHidGetExtraStat(usbd_hid_extra_stat_t *p_stat)
{
  p_stat->try_cnt   = extra_try_cnt;
  p_stat->sent_cnt  = extra_sent_cnt;
  p_stat->busy_cnt  = extra_busy_cnt;
  p_stat->fail_cnt  = extra_fail_cnt;
  p_stat->wait_cnt  = extra_wait_cnt;
  p_stat->over_cnt  = extra_over_cnt;
  p_stat->depth     = (uint8_t)(extra_wr - extra_rd);
  p_stat->depth_max = extra_depth_max;
}

/*
 * IF1(Extra) 로 보낸다. 첫 바이트가 리포트 ID 다.
 *
 * ★ 키보드와 같은 이유로 큐를 탄다 (위 Extra 큐 주석).
 *   TinyUSB 는 ID 를 따로 받으므로 ID 를 떼고 나머지를 담는다.
 */
bool usbdHidSendExtra(const uint8_t *p_report, uint16_t len)
{
  extra_item_t *p_item;
  uint8_t       depth;
  uint8_t       data_len;

  extra_try_cnt++;

  if (len < 2) return false;

  data_len = (uint8_t)(len - 1);
  if (data_len > EXTRA_DATA_MAX) data_len = EXTRA_DATA_MAX;

  if (usbdHidExtraIsFull() == true)
  {
    extra_wait_cnt++;
    usbdHidWaitRoom(usbdHidFlushExtra, usbdHidExtraIsFull);
  }

  if (usbdHidExtraIsFull() == true)
  {
    // 키보드 큐와 같은 정책 — 마지막 칸을 덮어 최종 상태를 살린다.
    extra_over_cnt++;
    p_item = &extra_queue[(extra_wr - 1) % EXTRA_QUEUE_MAX];
    p_item->id  = p_report[0];
    p_item->len = data_len;
    memcpy(p_item->data, &p_report[1], data_len);
    return false;
  }

  p_item = &extra_queue[extra_wr % EXTRA_QUEUE_MAX];
  p_item->id  = p_report[0];
  p_item->len = data_len;
  memcpy(p_item->data, &p_report[1], data_len);
  extra_wr++;

  depth = (uint8_t)(extra_wr - extra_rd);
  if (depth > extra_depth_max) extra_depth_max = depth;

  return usbdHidFlushExtra();
}

uint8_t usbdHidGetProtocol(void)
{
  return tud_hid_n_get_protocol(HID_ITF_KEYBOARD);
}

bool usbdHidSendMouse(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan)
{
  uint8_t report[6];

  // QMK 의 report_mouse_t 와 같은 모양이다 — { report_id, buttons, x, y, v, h }.
  // 큐를 태워야 컨슈머 · NKRO 와 순서가 지켜지고 busy 에 버려지지 않는다.
  report[0] = HID_REPORT_ID_MOUSE;
  report[1] = buttons;
  report[2] = (uint8_t)x;
  report[3] = (uint8_t)y;
  report[4] = (uint8_t)wheel;
  report[5] = (uint8_t)pan;

  return usbdHidSendExtra(report, sizeof(report));
}

/*
 * QMK 의 report_extra_t 와 같은 모양이다 — { report_id, usage(16bit) }.
 * IF1(Extra) 로 리포트 ID 4(컨슈머)를 달아 나간다.
 */
bool usbdHidSendConsumer(uint16_t usage)
{
  uint8_t report[3];

  report[0] = HID_REPORT_ID_CONSUMER;
  report[1] = (uint8_t)(usage & 0xFF);
  report[2] = (uint8_t)(usage >> 8);

  return usbdHidSendExtra(report, sizeof(report));
}

uint8_t usbdHidGetLed(void)
{
  return hid_led;
}


bool usbdHidGetRaw(uint8_t *p_data)
{
  if (raw_rd_count == raw_rx_count) return false;

  memcpy(p_data, raw_queue[raw_rd_count % HID_RAW_QUEUE_MAX], HID_RAW_REPORT_LEN);
  raw_rd_count++;

  return true;
}

bool usbdHidSendRaw(const uint8_t *p_data, uint16_t len)
{
  uint8_t  buf[HID_RAW_REPORT_LEN];
  uint32_t pre_time;

  if (len > HID_RAW_REPORT_LEN) len = HID_RAW_REPORT_LEN;

  memset(buf, 0, sizeof(buf));
  memcpy(buf, p_data, len);

  /*
   * ★ 여기서 delay() 를 쓰면 안 된다.
   *
   *   bsp.c 의 delay() 는 cliLoopIdle() 을 돌리고, 그 안에 qmkUpdate() 가 있다.
   *   VIA 처리 중에 다시 VIA 처리로 들어간다. tud_task() 만 직접 돌린다.
   */
  pre_time = millis();
  while (tud_hid_n_ready(HID_ITF_RAW) != true)
  {
    if (millis() - pre_time >= 10) return false;   /* 호스트가 안 읽으면 버린다 */
    tud_task();
  }

  return tud_hid_n_report(HID_ITF_RAW, 0, buf, HID_RAW_REPORT_LEN);
}

uint32_t usbdHidGetRawRxCount(void)   { return raw_rx_count; }
uint32_t usbdHidGetRawDropCount(void) { return raw_drop_count; }


//-- TinyUSB 콜백
//

const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance)
{
  uint16_t len;

  // 리포트 디스크립터 본체는 usbd_desc.c 에 있다.
  // config descriptor 가 sizeof() 로 길이를 넣어야 해서 같은 파일에 둔다.
  return usbdHidGetReportDesc(instance, &len);
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
  (void)instance;
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)reqlen;

  return 0;
}

// 호스트가 SET_PROTOCOL 로 boot / report 를 고른다.
//
// ★ QMK 는 이 값으로 NKRO 를 보낼지 정한다 (host_can_send_nkro()).
//   틀리면 부트 프로토콜만 아는 BIOS · 부트로더에서 키가 하나도 안 먹는다.
//   IF0(부트 키보드) 의 값을 QMK 에 그대로 알려 준다.
void tud_hid_set_protocol_cb(uint8_t instance, uint8_t protocol)
{
  if (instance != HID_ITF_KEYBOARD) return;

#ifdef QMK_ENABLE
  usb_device_state_set_protocol(protocol ? USB_PROTOCOL_REPORT : USB_PROTOCOL_BOOT);
#else
  (void)protocol;
#endif
}

void tud_mount_cb(void)
{
  // ★ 섀도와 큐를 버린다.
  //
  //   호스트가 새로 붙었으면 저쪽은 아무 키도 안 눌린 상태로 안다.
  //   우리 섀도가 옛 내용을 들고 있으면 "같으니 안 보낸다" 로 첫 리포트를
  //   통째로 삼킨다. 옛 호스트에게 보내려던 큐도 지금은 의미가 없다.
  kbd_shadow_valid = false;
  kbd_rd           = kbd_wr;
  extra_rd         = extra_wr;

#ifdef QMK_ENABLE
  usb_device_state_set_configuration(true, 1);
#endif
}

void tud_umount_cb(void)
{
#ifdef QMK_ENABLE
  usb_device_state_set_configuration(false, 0);
#endif
}

void tud_suspend_cb(bool remote_wakeup_en)
{
  (void)remote_wakeup_en;
#ifdef QMK_ENABLE
  usb_device_state_set_suspend(true, 1);
#endif
}

void tud_resume_cb(void)
{
  // ★ 여기서는 큐를 비우지 않는다.
  //
  //   suspend 는 연결이 끊긴 게 아니다. 호스트는 우리 키 상태를 그대로 기억하고
  //   있고, 큐에 **뗌**이 들어 있을 수 있다. 버리면 키가 눌린 채 남는다.
  //   섀도만 무효화해서 지금 상태를 한 번 더 싣게 한다.
  kbd_shadow_valid = false;

#ifdef QMK_ENABLE
  usb_device_state_set_resume(true, 1);
#endif
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
  (void)report_id;

  // 호스트가 보내는 키보드 LED 상태 (CapsLock / NumLock / ScrollLock)
  if (instance == HID_ITF_KEYBOARD &&
      report_type == HID_REPORT_TYPE_OUTPUT &&
      bufsize >= 1)
  {
    hid_led = buffer[0];
#ifdef QMK_ENABLE
    usb_device_state_set_leds(hid_led);
#endif
    return;
  }

  // raw HID (VIA · Vial)
  //
  // TUD_HID_INOUT_DESCRIPTOR 로 만든 인터럽트 OUT 엔드포인트로 온 것은
  // report_type 이 0(INVALID)이다. SET_REPORT 제어 전송으로 오면 OUTPUT 이다.
  // 둘 다 받는다.
  if (instance == HID_ITF_RAW && bufsize > 0)
  {
    if (raw_rx_count - raw_rd_count >= HID_RAW_QUEUE_MAX)
    {
      // 넘치면 버린다. 막으면 USB 콜백 안에서 굶는다.
      raw_drop_count++;
      return;
    }

    uint16_t len = (bufsize > HID_RAW_REPORT_LEN) ? HID_RAW_REPORT_LEN : bufsize;

    memset(raw_queue[raw_rx_count % HID_RAW_QUEUE_MAX], 0, HID_RAW_REPORT_LEN);
    memcpy(raw_queue[raw_rx_count % HID_RAW_QUEUE_MAX], buffer, len);
    raw_rx_count++;
  }
}

#endif
