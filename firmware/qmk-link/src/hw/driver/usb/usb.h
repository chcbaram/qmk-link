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

// BOOTSEL 로 재부팅한다. 돌아오지 않는다.
void usbRebootBootsel(void);


#endif

#ifdef __cplusplus
 }
#endif

#endif /* USB_H_ */
