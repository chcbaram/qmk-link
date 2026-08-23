#pragma once

// qmk-link 의 QMK 설정.
//
// 이 프로젝트는 물리 매트릭스가 없다. USB-A 에 꽂힌 키보드가 보낸 HID usage 를
// 그대로 좌표로 쓴다 (ap/modules/link).
//
//   row = usage >> 4,  col = usage & 0x0F   ->  16 x 16 = 256 키
//
// HID usage 는 0x00~0xFF 라 이 크기면 전부 담긴다.

#define KBD_NAME                    "QMK-LINK"

#define USB_VID                     HW_USB_VID
#define USB_PID                     HW_USB_PID

#define MATRIX_ROWS                 16
#define MATRIX_COLS                 16

// 디바운스는 쓰지 않는다. 원본 키보드가 이미 했고,
// 우리 matrix_scan() 은 샘플링이 아니라 마지막 리포트를 읽을 뿐이다.
#define DEBOUNCE                    0

#define DYNAMIC_KEYMAP_LAYER_COUNT  8

// eeprom — 내장 플래시 0x1F0000 부터 16KB (hw_def.h 의 HW_FLASH_E2P_VIA_*)
//
//   dynamic keymap  8레이어 x 16 x 16 x 2B = 4096B
//   나머지는 eeconfig · VIA user data · 매크로 버퍼가 나눠 쓴다
//
// Vial 트리는 같은 크기를 0x1F4000 에 따로 갖는다. 섞이면 안 된다.
#define EECONFIG_USER_DATA_SIZE     64
#define TOTAL_EEPROM_BYTE_COUNT     16384

