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

// ★ 플래시 작업 뒤에 반드시 부른다 (hw/driver/flash.c 가 부른다).
//
//   flash_safe_execute() 가 core1 을 수십 ms 세운다. 그 사이 SOF 가 끊겨
//   장치가 서스펜드에 빠지고, 깨어나지 못한 채 전송이 계속 실패한다.
//   TinyUSB 의 hidh_xfer_cb 는 실패를 무시하고(`(void) result;`) 길이 0 으로
//   콜백하므로, 겉보기에는 리포트가 계속 오는데 내용이 없다.
//   리셋 전까지 회복되지 않는다 — 실측으로 확인했다.
//
//   그래서 "뗐다 붙였다" 를 usbh 에 알려 열거를 처음부터 다시 시킨다.
//   열거가 포트 리셋(SE0)을 내보내고, 그게 서스펜드에 빠진 장치를 깨운다.
//
//   논블로킹이다 — 요청만 세우고 core1 이 처리한다.
void     usbhRequestRecover(void);
uint32_t usbhGetRecoverCount(void);


#endif

#ifdef __cplusplus
 }
#endif

#endif /* USBH_H_ */
