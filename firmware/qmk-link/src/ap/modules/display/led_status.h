#ifndef LED_STATUS_H_
#define LED_STATUS_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "ap_def.h"


typedef enum
{
  LED_ST_SUSPEND = 0,   // PC 가 잠들었다 — 소등
  LED_ST_NO_PC,         // Type-C 가 PC 에 안 붙었다
  LED_ST_NO_KBD,        // PC 는 붙었는데 USB-A 에 키보드가 없다
  LED_ST_READY,         // 둘 다 붙었다
  LED_ST_MAX
} led_status_t;


bool         ledStatusInit(void);

// ap 메인 루프에서 부른다.
void         ledStatusUpdate(void);

// 키가 눌렸을 때 부른다. 짧게 반짝인다.
void         ledStatusKeyEvent(void);

led_status_t ledStatusGet(void);


#ifdef __cplusplus
 }
#endif

#endif /* LED_STATUS_H_ */
