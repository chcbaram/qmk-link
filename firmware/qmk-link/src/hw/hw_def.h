#ifndef HW_DEF_H_
#define HW_DEF_H_


#include "bsp.h"


#define _DEF_FIRMWATRE_VERSION    "V260823R3"
// ★ 이름 뒤에 트리를 붙인다 (KEY_PROTOCOL_NAME 은 CMake 가 준다).
//   USB 제품 이름이 "QMK-LINK VIA" / "QMK-LINK VIAL" 로 갈려서
//   지금 어느 펌웨어가 올라가 있는지 OS 에서 바로 보인다.
#ifdef KEY_PROTOCOL_NAME
#define _DEF_BOARD_NAME           "QMK-LINK " KEY_PROTOCOL_NAME
#else
#define _DEF_BOARD_NAME           "QMK-LINK"
#endif


//-- USB
//
// 다른 baram 키보드와 겹치지 않는 값.
//   0x5200 hs-k / 0x5201 45k-hs / 0x5207 qmk-8k / 0x5211 convex
//   0x5220 Lucky65 / 0x5230 hola-mini / 0x5300 esp32-qmk / 0x5301 qmk-h7s
//   0x5304 wish-he  <- 직전 최신
//
#define _USE_HW_USB
#define      HW_USB_VID             0x0483
#define      HW_USB_PID             0x5305

//-- FLASH
//
// W25Q16JV = 2MB. 끝 64KB 를 데이터 영역으로 예약한다 (펌웨어는 여기까지 오지 않는다).
//
//   0x1D0000  키보드 레이아웃 저장소  128KB (8KB x 16칸)
//   0x1F0000  VIA  EEPROM  16KB (4섹터)
//   0x1F4000  Vial EEPROM  16KB (4섹터)
//   0x1F8000  (예약)       28KB
//   0x1FF000  flash test    4KB   <- CLI `flash test` 가 쓰는 자리
//
// ★ VIA 와 Vial 을 다른 자리에 두는 이유
//
//   같은 보드에 두 펌웨어를 번갈아 구울 수 있다. 한 영역을 공유하면 트리를 바꿔
//   구운 순간 상대가 남긴 바이트를 자기 레이아웃으로 읽는다. eeconfig 매직이
//   우연히 맞으면 초기화도 안 되고 엉뚱한 키맵이 나온다. 아예 떼어 놓는다.
//
#define _USE_HW_FLASH
#define      HW_FLASH_SECTOR_SIZE     4096         /* 소거 단위 */
#define      HW_FLASH_PAGE_SIZE       256          /* 기록 단위 */
#define      HW_FLASH_SIZE            (2*1024*1024)
#define      HW_FLASH_USER_BEGIN      0x1D0000UL   /* 이 아래로는 쓰지 않는다 */

// 꽂힌 키보드마다 레이아웃 정의를 담아 둔다 (09단계).
//
// ★ 한 칸이 섹터(4KB)의 배수여야 한다. 소거 단위가 섹터라 그렇다.
//   8KB 면 압축된 Vial 정의(실측 552B)가 넉넉히 들어가고, 비압축이어도 남는다.
#define      HW_FLASH_KBD_BEGIN       0x1D0000UL
#define      HW_FLASH_KBD_SLOT_SIZE   0x002000UL   /* 8KB */
#define      HW_FLASH_KBD_SLOT_MAX    16           /* 합 128KB */
// 키보드마다 "어느 SLOT 을 적용할지" 를 기억한다 (09단계).
//
// ★ EEPROM 에 두면 안 된다.
//
//   EEPROM 은 트리마다 갈라져 있다 (아래 VIA / VIAL). 거기 두면 펌웨어를
//   바꿔 구운 순간 선택이 사라진다. SLOT 저장소를 공용으로 둔 것과 같은
//   이유로 선택도 공용 영역이어야 한다.
//
// ★ 추가 기록(append-only) 이다. 바꿀 때마다 16B 를 덧붙이고 읽을 때
//   마지막 것을 쓴다. 슬롯 blob(8KB) 을 다시 굽지 않아도 된다.
#define      HW_FLASH_KBD_SEL_BEGIN   0x1F8000UL
#define      HW_FLASH_KBD_SEL_SIZE    0x001000UL   /* 4KB = 16B x 256 */

#define      HW_FLASH_E2P_VIA_BEGIN   0x1F0000UL
#define      HW_FLASH_E2P_VIA_SIZE    0x004000UL
#define      HW_FLASH_E2P_VIAL_BEGIN  0x1F4000UL
#define      HW_FLASH_E2P_VIAL_SIZE   0x004000UL

// ★ CLI `flash test` 전용 섹터. 다른 용도로 잡지 않는다.
//   소거/기록 시간을 재고 core1 이 계속 도는지 확인하는 데 쓴다.
#define      HW_FLASH_TEST_BEGIN      0x1FF000UL

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
#define _USE_CLI_HW_FLASH           1
#define _USE_CLI_AP_KBD             1


#endif
