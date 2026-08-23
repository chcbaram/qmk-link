#include "usbd_hid.h"

#ifdef _USE_HW_USB
#include "tusb.h"






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

bool usbdHidSendKeyboard(const uint8_t *p_report)
{
  if (kbd_shadow_valid && memcmp(kbd_shadow, p_report, 8) == 0)
  {
    return true;
  }

  if (tud_hid_n_ready(HID_ITF_KEYBOARD) != true) return false;

  memcpy(kbd_shadow, p_report, 8);
  kbd_shadow_valid = true;

  // 리포트 ID 를 안 쓰므로 0 을 넘긴다. 8바이트가 그대로 나간다.
  return tud_hid_n_report(HID_ITF_KEYBOARD, 0, p_report, 8);
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
  // ★ 여기서 늘 1 을 돌려주면 QMK 가 NKRO 만 보내서
  //   부트 프로토콜만 아는 BIOS · 부트로더에서 키가 하나도 안 먹는다.
  //   호스트가 SET_PROTOCOL 로 정한 값을 그대로 준다. (wish-he 의 실측 기록)
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
    return;
  }

  // VIA · Vial 은 06단계에서 채운다.
}

#endif
