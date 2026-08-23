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

// ★ 전송 정책 — "바뀔 때만 싣는다"
//
// QMK 는 같은 리포트를 여러 번 보낼 수 있다 (레이어 처리 중간 상태 등).
// 그때마다 전송을 걸면 옛 리포트가 먼저 나가느라 지연이 오히려 늘어난다.
// 섀도와 비교해 같으면 아무것도 하지 않는다. (wish-he 의 driver_usb.c 관례)
static uint8_t kbd_shadow[8];
static bool    kbd_shadow_valid = false;

// 진단용 — PC 로 나가는 키보드 리포트가 어디서 막히는지 센다.
static volatile uint32_t kbd_try_cnt  = 0;   // 호출
static volatile uint32_t kbd_same_cnt = 0;   // 섀도와 같아 건너뜀
static volatile uint32_t kbd_busy_cnt = 0;   // tud_hid_n_ready 가 false
static volatile uint32_t kbd_sent_cnt = 0;   // tud_hid_n_report 성공
static volatile uint32_t kbd_fail_cnt = 0;   // tud_hid_n_report 실패
static volatile uint32_t kbd_retry_cnt = 0;  // 보류분을 나중에 보낸 횟수

// ★ 못 보낸 리포트는 버리지 않고 들고 있는다.
//
//   엔드포인트가 바쁘면(raw HID 가 몰릴 때 실제로 생긴다 — 실측 not ready 13)
//   예전에는 그대로 버렸다. QMK 는 재시도하지 않으므로 그 키 이벤트는 영영
//   사라진다. 눌림이 유실되면 안 눌린 것이 되고, 뗌이 유실되면 PC 에 키가
//   눌린 채로 남는다.
//
//   키보드 리포트는 "지금 눌린 상태" 라 최신 것만 있으면 된다.
//   그래서 큐가 아니라 한 칸이면 충분하다.
static uint8_t kbd_pending[8];
static bool    kbd_pending_valid = false;

// raw HID 수신 큐. 호스트 -> 우리 방향만 큐를 탄다 (usbd_hid.h 주석 참고).
#define HID_RAW_QUEUE_MAX   8
static uint8_t           raw_queue[HID_RAW_QUEUE_MAX][HID_RAW_REPORT_LEN];
static volatile uint32_t raw_rx_count   = 0;
static volatile uint32_t raw_rd_count   = 0;
static volatile uint32_t raw_drop_count = 0;

/*
 * 보류 중인 리포트를 내보낸다. 메인 루프가 계속 부른다 (usbdHidUpdate).
 *
 * ★ 섀도는 실제로 나간 뒤에 갱신한다.
 *
 *   먼저 갱신하면 전송이 실패했을 때 "이미 보냈다" 고 기억해서 같은 리포트를
 *   영영 다시 안 보낸다.
 */
static bool usbdHidFlushKeyboard(void)
{
  bool ret;

  if (kbd_pending_valid != true) return true;

  if (tud_hid_n_ready(HID_ITF_KEYBOARD) != true)
  {
    kbd_busy_cnt++;
    return false;
  }

  // 리포트 ID 를 안 쓰므로 0 을 넘긴다. 8바이트가 그대로 나간다.
  ret = tud_hid_n_report(HID_ITF_KEYBOARD, 0, kbd_pending, 8);

  if (ret != true)
  {
    kbd_fail_cnt++;
    return false;
  }

  memcpy(kbd_shadow, kbd_pending, 8);
  kbd_shadow_valid  = true;
  kbd_pending_valid = false;
  kbd_sent_cnt++;

  return true;
}

void usbdHidUpdate(void)
{
  if (kbd_pending_valid == true)
  {
    if (usbdHidFlushKeyboard() == true) kbd_retry_cnt++;
  }
}

bool usbdHidSendKeyboard(const uint8_t *p_report)
{
  kbd_try_cnt++;

  if (kbd_shadow_valid && memcmp(kbd_shadow, p_report, 8) == 0)
  {
    kbd_same_cnt++;
    return true;
  }

  // 앞의 보류분을 먼저 내보낸다. 중간 상태를 건너뛰지 않으려는 것이다.
  usbdHidFlushKeyboard();

  memcpy(kbd_pending, p_report, 8);
  kbd_pending_valid = true;

  return usbdHidFlushKeyboard();
}

void usbdHidGetKbdStat(usbd_hid_kbd_stat_t *p_stat)
{
  p_stat->try_cnt  = kbd_try_cnt;
  p_stat->same_cnt = kbd_same_cnt;
  p_stat->busy_cnt = kbd_busy_cnt;
  p_stat->sent_cnt = kbd_sent_cnt;
  p_stat->fail_cnt  = kbd_fail_cnt;
  p_stat->retry_cnt = kbd_retry_cnt;
  p_stat->pending   = kbd_pending_valid;
  p_stat->is_ready = tud_hid_n_ready(HID_ITF_KEYBOARD);
  p_stat->is_mount = tud_mounted();
  p_stat->is_susp  = tud_suspended();
}

bool usbdHidSendExtra(const uint8_t *p_report, uint16_t len)
{
  if (tud_hid_n_ready(HID_ITF_EXTRA) != true) return false;

  // QMK 구조체의 첫 바이트가 리포트 ID 다. TinyUSB 는 ID 를 따로 받으므로
  // ID 를 떼고 나머지를 넘긴다.
  return tud_hid_n_report(HID_ITF_EXTRA, p_report[0], &p_report[1], len - 1);
}

uint8_t usbdHidGetProtocol(void)
{
  return tud_hid_n_get_protocol(HID_ITF_KEYBOARD);
}

bool usbdHidSendMouse(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan)
{
  if (tud_hid_n_ready(HID_ITF_EXTRA) != true) return false;

  return tud_hid_n_mouse_report(HID_ITF_EXTRA, HID_REPORT_ID_MOUSE,
                                buttons, x, y, wheel, pan);
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
  // ★ 섀도를 버린다.
  //
  //   호스트가 새로 붙었으면 저쪽은 아무 키도 안 눌린 상태로 안다.
  //   우리 섀도가 옛 내용을 들고 있으면 "같으니 안 보낸다" 로 첫 리포트를
  //   통째로 삼킨다.
  kbd_shadow_valid  = false;
  kbd_pending_valid = false;

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
