/*
 * qmk_link.h
 *
 * RP2350-USB-A 보드 정의
 *
 *   MCU    : RP2350A (QFN60)
 *   Flash  : W25Q16JVUXIQ (2MB)
 *   XOSC   : 12MHz
 *   WS2812 : GPIO16 (L1)
 *   USB-A  : D+ = GPIO12, D- = GPIO13 (J1, PIO USB host)
 *   Type-C : USB_DP(52) / USB_DM(51) 네이티브 USB
 *
 * 자세한 내용은 firmware/docs/HARDWARE.md 참고.
 */

// -----------------------------------------------------
// NOTE: 이 헤더는 어셈블러도 포함하므로
//       전처리기 지시자만 있어야 한다.
// -----------------------------------------------------

// pico_cmake_set PICO_PLATFORM=rp2350

#ifndef _BOARDS_QMK_LINK_H
#define _BOARDS_QMK_LINK_H

// 보드 감지용
#define QMK_LINK

// --- RP2350 VARIANT ---
#define PICO_RP2350A 1

// --- WS2812 (L1) ---
#define QMK_LINK_WS2812_PIN         16

// --- USB-A host port (J1) ---
// Pico-PIO-USB 는 D- 가 D+ 다음 핀이어야 한다. 12 / 13 으로 만족한다.
#define QMK_LINK_USB_HOST_DP_PIN    12
#define QMK_LINK_USB_HOST_DM_PIN    13

// --- 이 보드에 없는 것 ---
// 디버그 UART 핀이 없다. PICO_DEFAULT_UART* 를 정의하지 않는다.
// 단순 GPIO LED 가 없다. PICO_DEFAULT_LED_PIN 을 정의하지 않는다.

// --- FLASH : W25Q16JVUXIQ = 2MB ---
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

// pico_cmake_set_default PICO_FLASH_SIZE_BYTES = (2 * 1024 * 1024)
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

#endif
