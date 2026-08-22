#ifndef HW_DEF_H_
#define HW_DEF_H_


#include "bsp.h"


#define _DEF_FIRMWATRE_VERSION    "V260823R2"
#define _DEF_BOARD_NAME           "QMK-LINK"


//-- USB
//
// 개발용 임시 VID/PID. 08단계에서 확정하고 info.json / vial.json 과 맞춘다.
//
#define _USE_HW_USB
#define      HW_USB_VID             0x2E8A      // Raspberry Pi
#define      HW_USB_PID             0xF001

//-- RESET
//
#define _USE_HW_RESET

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

//-- UART
//
// 이 보드에는 디버그 UART 핀이 없다. 채널은 USB CDC 하나뿐이다.
//
#define _USE_HW_UART
#define      HW_UART_MAX_CH         1
#define      HW_UART_CH_USB         _DEF_UART1
#define      HW_UART_CH_CLI         HW_UART_CH_USB
#define      HW_UART_CH_LOG         HW_UART_CH_USB

//-- CLI
//
#define _USE_HW_CLI
#define      HW_CLI_CMD_LIST_MAX    24
#define      HW_CLI_CMD_NAME_MAX    16
#define      HW_CLI_LINE_HIS_MAX    8
#define      HW_CLI_LINE_BUF_MAX    64

#define _USE_HW_CLI_GUI
#define      HW_CLI_GUI_WIDTH       80
#define      HW_CLI_GUI_HEIGHT      24

//-- SWTIMER
//
#define _USE_HW_SWTIMER
#define      HW_SWTIMER_MAX_CH      8

//-- LOG
//
#define _USE_HW_LOG
#define      HW_LOG_CH              HW_UART_CH_LOG
#define      HW_LOG_BOOT_BUF_MAX    2048
#define      HW_LOG_LIST_BUF_MAX    1024


//-- CLI 명령 on/off
//
#define _USE_CLI_HW_RESET           1


#endif
