#ifndef USBH_H_
#define USBH_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_USBH
#include "usbh_hid/usbh_hid.h"


// core0 에서 부른다. core1 을 띄우고 거기서 tuh_task() 를 돌린다.
//
// PIO USB 는 타이밍에 민감해서 전용 코어를 쓴다 (공식 예제도 그렇다).
// core0 은 tud_task() 와 CLI 를, core1 은 tuh_task() 만 돌린다.
bool usbhInit(void);

// core1 이 살아서 돌고 있나
bool usbhIsRunning(void);

uint32_t usbhGetTaskCount(void);


#endif

#ifdef __cplusplus
 }
#endif

#endif /* USBH_H_ */
