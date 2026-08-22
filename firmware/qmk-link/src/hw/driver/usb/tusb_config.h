#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
 extern "C" {
#endif

//--------------------------------------------------------------------
// BOARD
//--------------------------------------------------------------------

// RHPort 0 = RP2350 네이티브 USB (Type-C) -> device
// RHPort 1 = PIO USB (GPIO12/13, USB-A)   -> host
#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT          0
#endif

#ifndef BOARD_TUD_MAX_SPEED
#define BOARD_TUD_MAX_SPEED       OPT_MODE_DEFAULT_SPEED
#endif

#ifndef BOARD_TUH_RHPORT
#define BOARD_TUH_RHPORT          1
#endif

#ifndef BOARD_TUH_MAX_SPEED
#define BOARD_TUH_MAX_SPEED       OPT_MODE_FULL_SPEED
#endif

// RHPort1 은 하드웨어가 없다. PIO 로 만든다.
#define CFG_TUSB_RHPORT1_MODE     (OPT_MODE_HOST | OPT_MODE_FULL_SPEED)
#define CFG_TUH_RPI_PIO_USB       1

//--------------------------------------------------------------------
// COMMON
//--------------------------------------------------------------------

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS               OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG            0
#endif

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN        __attribute__ ((aligned(4)))
#endif

//--------------------------------------------------------------------
// DEVICE
//--------------------------------------------------------------------

#define CFG_TUD_ENABLED           1
#define CFG_TUD_MAX_SPEED         BOARD_TUD_MAX_SPEED

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE    64
#endif

//------------- CLASS -------------//
// 04단계에서 CFG_TUD_HID 3 (keyboard / extra / raw) 이 여기 붙는다.
#define CFG_TUD_CDC               1
#define CFG_TUD_HID               0
#define CFG_TUD_MSC               0
#define CFG_TUD_MIDI              0
#define CFG_TUD_VENDOR            0

#define CFG_TUD_CDC_RX_BUFSIZE    256
#define CFG_TUD_CDC_TX_BUFSIZE    256
#define CFG_TUD_CDC_EP_BUFSIZE    64

//--------------------------------------------------------------------
// HOST  (USB-A, PIO USB)
//--------------------------------------------------------------------

#define CFG_TUH_ENABLED           1
#define CFG_TUH_MAX_SPEED         BOARD_TUH_MAX_SPEED

// 허브를 켠다.
// 키보드 안에 허브가 들어 있는 경우가 있다 (예: HHKB Lite 2 는 뒷면에 USB 포트가 있다).
// 끄면 그런 키보드는 연결은 감지되지만 열거가 끝나지 않는다.
#define CFG_TUH_HUB               1
#define CFG_TUH_DEVICE_MAX        (3 * CFG_TUH_HUB + 1)

// 키보드 하나가 인터페이스를 여러 개 낼 수 있다 (boot kbd + consumer 등).
#define CFG_TUH_HID               4
#define CFG_TUH_HID_EPIN_BUFSIZE  64
#define CFG_TUH_HID_EPOUT_BUFSIZE 64

#define CFG_TUH_CDC               0
#define CFG_TUH_MSC               0
#define CFG_TUH_VENDOR            0

#define CFG_TUH_ENUMERATION_BUFSIZE 256

#ifdef __cplusplus
 }
#endif

#endif /* _TUSB_CONFIG_H_ */
