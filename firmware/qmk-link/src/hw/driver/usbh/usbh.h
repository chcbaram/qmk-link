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

// ★ 여기에 있던 usbhRequestRecover() 는 없앴다.
//
//   flash 정지 뒤 "뗐다 붙었다" 를 usbh 에 알려 재열거시키려 했는데,
//   떼기만 하고 다시 붙지 못했다 (connect st 1 / speed full 인데 mounted 0,
//   자체 회복도 안 됨). 애초에 정지를 없애는 쪽이 맞다 —
//   지금은 copy_to_ram 으로 정지 자체가 없다 (src/CMakeLists.txt).


#endif

#ifdef __cplusplus
 }
#endif

#endif /* USBH_H_ */
