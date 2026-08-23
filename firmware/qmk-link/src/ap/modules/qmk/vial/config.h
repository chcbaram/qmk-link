#pragma once

// Vial 트리의 QMK 설정.
//
// 공용 설정은 keyboards/qmk-link/config.h 에 있다 (via 트리와 같은 것을 본다).
// 여기에는 Vial 고유의 것만 둔다.

#include QMK_BOARD_CONFIG_H


// ★ BUILD_ID — Vial 에서 EEPROM 유효성 매직으로 쓰인다 (via.c 의 via_eeprom_is_valid).
//
//   vial-qmk 의 util/build_id.py 는 이 값을 **빌드마다 난수로** 만든다.
//   그러면 다시 구울 때마다 EEPROM 이 무효가 되어 키맵이 초기화된다.
//   여기서는 고정값을 박는다 — EEPROM 배치를 바꿀 때만 손으로 올린다.
#define BUILD_ID ((uint32_t)0x00514C4B)   /* "QLK" */


// ★ 키보드 UID — Vial 앱이 장치를 구별하는 값이다.
//
//   vial-qmk 의 `vial generate-keyboard-uid` 가 8바이트 난수를 뽑는 것과 같다.
//   한 번 정하면 바꾸지 않는다. 바꾸면 앱이 다른 키보드로 본다.
#define VIAL_KEYBOARD_UID {0x6B, 0xF3, 0x8A, 0x88, 0x58, 0x4F, 0x4F, 0x39}


// ★ unlock 조합 — 이 키들을 몇 초 누르고 있어야 편집이 열린다.
//
//   좌표가 HID usage 라 물리 위치와 무관하다. 그래서 고르는 기준이 다르다 —
//   **어떤 키보드에나 있는 키**여야 한다. 60% 에는 오른쪽 Ctrl 이 없는 것도 있다.
//
//   좌우 Shift 로 정했다. 없는 키보드가 사실상 없고, 둘을 몇 초씩 함께 누르는 일도
//   실수로는 잘 일어나지 않는다.
//
//     LSFT usage 0xE1 -> row 14, col 1
//     RSFT usage 0xE5 -> row 14, col 5
#define VIAL_UNLOCK_COMBO_ROWS {14, 14}
#define VIAL_UNLOCK_COMBO_COLS { 1,  5}
