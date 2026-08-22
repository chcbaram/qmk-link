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

bool usbdHidSendKeyboard(const uint8_t *p_report)
{
  if (tud_hid_n_ready(HID_ITF_KEYBOARD) != true) return false;

  // 리포트 ID 를 안 쓰므로 0 을 넘긴다. 8바이트가 그대로 나간다.
  return tud_hid_n_report(HID_ITF_KEYBOARD, 0, p_report, 8);
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
