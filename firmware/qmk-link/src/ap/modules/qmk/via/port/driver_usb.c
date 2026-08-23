/*
 * port/driver_usb.c  —  QMK 의 출력 드라이버를 우리 TinyUSB 로 잇는다.
 *
 * host.c 의 host_keyboard_send() 등이 활성 드라이버의 함수 포인터로 넘어온다.
 *
 * ★ 전송 정책은 7편 그대로다 — "바뀔 때만 싣는다".
 *
 *   hidKbdSetReportRaw() 가 섀도와 비교해 같으면 아무것도 하지 않는다. QMK 는
 *   같은 리포트를 여러 번 보낼 수 있는데(레이어 처리 중간 상태 등) 그때마다
 *   전송을 걸면 옛 리포트가 먼저 나가느라 지연이 오히려 늘어난다.
 *
 * NKRO · 마우스 · 미디어키는 IF1(hid_exk_if.c) 하나에 리포트 ID 로 나눠 실린다.
 * 인터페이스를 새로 더하면 CDC 의 번호가 밀리므로(7편의 등록 순서 함정) ID 만 얹었다.
 */

#include "host.h"
#include "host_driver.h"
#include "report.h"
#include "keycode_config.h"
#include "usbd_hid.h"


static uint8_t usb_keyboard_leds(void)
{
  return usbdHidGetLed();
}

static void usb_send_keyboard(report_keyboard_t *report)
{
  /* report_keyboard_t = { mods, reserved, keys[6] } — 부트 리포트와 같은 8바이트 */
  usbdHidSendKeyboard((const uint8_t *)report);
}

static void usb_send_nkro(report_nkro_t *report)
{
#ifdef NKRO_ENABLE
  /* report_nkro_t = { report_id, mods, bits[30] } — IF1 의 ID 1 배치와 같다 */
  usbdHidSendExtra((const uint8_t *)report, sizeof(report_nkro_t));
#else
  (void)report;
#endif
}

static void usb_send_mouse(report_mouse_t *report)
{
#ifdef MOUSE_ENABLE
  /* report_mouse_t = { report_id, buttons, x, y, v, h } — IF1 의 ID 2 배치와 같다.
   * host_mouse_send() 가 MOUSE_SHARED_EP 를 보고 report_id 를 채워 준다. */
  usbdHidSendExtra((const uint8_t *)report, sizeof(report_mouse_t));
#else
  (void)report;
#endif
}

static void usb_send_extra(report_extra_t *report)
{
#ifdef EXTRAKEY_ENABLE
  /* report_extra_t = { report_id, usage } — 시스템(3) / 컨슈머(4) 둘 다 이 모양 */
  usbdHidSendExtra((const uint8_t *)report, sizeof(report_extra_t));
#else
  (void)report;
#endif
}

host_driver_t usb_driver = {
  usb_keyboard_leds,
  usb_send_keyboard,
  usb_send_nkro,
  usb_send_mouse,
  usb_send_extra,
};



/*
 * ★ keyboard_protocol / host_can_send_nkro 는 여기서 만들지 않는다.
 *
 *   wish-he · hola-mini 가 쓰던 2024-04 판 QMK 는 keyboard_protocol 전역을 썼고,
 *   그걸 늘 1 로 두면 BIOS 에서 키가 안 먹는 함정이 있었다.
 *
 *   QMK 0.33 은 그 자리를 usb_device_state 로 대체했다. host_can_send_nkro() 도
 *   upstream 의 host.c 가 usb_device_state_get_protocol() 로 판단한다.
 *   우리가 할 일은 TinyUSB 의 SET_PROTOCOL / 연결 상태를 그쪽에 알려 주는 것뿐이고,
 *   그건 hw/driver/usb/usbd_hid.c 의 콜백이 한다.
 */
