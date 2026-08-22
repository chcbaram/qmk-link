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

//-- USB HOST (USB-A, PIO USB)
//
#define _USE_HW_USBH
#define      HW_USBH_RHPORT         1
// Pico-PIO-USB 의 pin_dp 는 GPIO12, D- 는 그 다음 핀(13)이다.
// 이 조합으로 실기에서 열거가 된다.
#define      HW_USBH_DP_PIN         QMK_LINK_USB_HOST_DP_PIN   // GPIO12
#define      HW_USBH_PINOUT         0      // PIO_USB_PINOUT_DPDM : D- = pin_dp + 1
#define      HW_USBH_ALARM_NUM      2      // SDK 기본 알람풀(3번)과 겹치지 않게
#define      USBH_HID_REPORT_MAX    64
#define      USBH_HID_QUEUE_MAX     32

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
// PIO USB 가 pio0 를 통째로 쓴다 (sm0 TX / sm1 RX / sm2 EOP). 그래서 pio1 이다.
#define      HW_WS2812_PIO          pio1
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
