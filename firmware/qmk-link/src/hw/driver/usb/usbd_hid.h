#ifndef USBD_HID_H_
#define USBD_HID_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_USB


// HID 인터페이스 (TinyUSB 의 instance 번호와 같아야 한다)
enum
{
  HID_ITF_KEYBOARD = 0,   // boot protocol. 리포트 ID 없음
  HID_ITF_EXTRA,          // mouse / system / consumer / NKRO. 리포트 ID 로 나눈다
  HID_ITF_RAW,            // VIA · Vial (usage page 0xFF60)
  HID_ITF_COUNT
};

// 리포트 ID. QMK 의 enum hid_report_ids 와 같아야 한다.
// 05단계에서 QMK 를 올릴 때 어긋나면 곤란하다.
enum
{
  HID_REPORT_ID_KEYBOARD = 1,
  HID_REPORT_ID_MOUSE,        // 2
  HID_REPORT_ID_SYSTEM,       // 3
  HID_REPORT_ID_CONSUMER,     // 4
  HID_REPORT_ID_PROGRAMMABLE, // 5
  HID_REPORT_ID_NKRO,         // 6
};

#define HID_NKRO_KEY_BYTES    30                        // 240키
#define HID_NKRO_REPORT_LEN   (1 + HID_NKRO_KEY_BYTES)  // 모디파이어 + 비트맵
#define HID_RAW_REPORT_LEN    32


bool usbdHidInit(void);

// 호스트가 붙어서 리포트를 받을 준비가 됐나
bool usbdHidIsReady(uint8_t itf);

// boot keyboard 리포트 8바이트를 그대로 보낸다 (패스스루용)
bool usbdHidSendKeyboard(const uint8_t *p_report);

// 마우스. buttons/x/y/wheel/pan
bool usbdHidSendMouse(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan);

// 호스트가 보낸 LED 상태 (CapsLock 등). HID_KEYBOARD_LED_* 비트
uint8_t usbdHidGetLed(void);

const uint8_t *usbdHidGetReportDesc(uint8_t itf, uint16_t *p_len);


#endif

#ifdef __cplusplus
 }
#endif

#endif /* USBD_HID_H_ */
