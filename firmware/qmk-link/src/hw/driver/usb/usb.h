#ifndef USB_H_
#define USB_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_USB


bool usbInit(void);
void usbUpdate(void);
bool usbIsOpen(void);
bool usbIsConnect(void);
bool usbIsSuspended(void);

// PC 에 보고할 PID 를 바꾼다. 값이 달라지면 스스로 재열거한다 (usb.c 주석 참고).
// 블록하지 않는다 — 끊고 붙이는 사이는 usbUpdate() 가 센다.
void     usbSetProductId(uint16_t pid);
uint16_t usbGetProductId(void);

// 예약된 재열거를 뒤로 민다. 우리 도구가 말을 걸고 있는 동안은 끊지 않는다.
void     usbPostponeReenum(void);
bool     usbIsReenumPending(void);

// BOOTSEL 로 재부팅한다. 돌아오지 않는다.
void usbRebootBootsel(void);


#endif

#ifdef __cplusplus
 }
#endif

#endif /* USB_H_ */
