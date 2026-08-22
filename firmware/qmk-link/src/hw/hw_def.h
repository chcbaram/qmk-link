#ifndef HW_DEF_H_
#define HW_DEF_H_


#include "bsp.h"


#define _DEF_FIRMWATRE_VERSION    "V260823R1"
#define _DEF_BOARD_NAME           "QMK-LINK"


//-- LED
//
// 이 보드에는 단순 GPIO LED 가 없다. led.c 는 ws2812 위에 올라간 래퍼다.
//
#define _USE_HW_LED
#define      HW_LED_MAX_CH          1

//-- WS2812
//
#define _USE_HW_WS2812
#define      HW_WS2812_MAX_CH       1
#define      HW_WS2812_PIN          QMK_LINK_WS2812_PIN     // GPIO16 (L1)
#define      HW_WS2812_PIO          pio0
#define      HW_WS2812_SM           0
// 이 보드의 WS2812B-0807 은 전송 순서가 R,G,B 다.
// 표준 WS2812B 의 G,R,B 가 아니다 — 실기에서 확인했다.
#define      HW_WS2812_ORDER_RGB


#endif
