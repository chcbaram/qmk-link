// SPDX-License-Identifier: GPL-2.0-or-later
//
// 가상 매트릭스 keymap.
//
// 좌표가 HID usage 다.  row = usage >> 4,  col = usage & 0x0F
// 예) KC_A 는 usage 0x04 -> [0][4]
//
// 기본 레이어는 "들어온 키를 그대로 내보낸다". 원본 키보드의 배열을 그대로 쓴다는 뜻이다.
// 바꾸고 싶은 자리만 고치면 된다.

#include QMK_KEYBOARD_H


// usage 를 그대로 keycode 로 쓴다. QMK 의 기본 키코드는 HID usage 와 값이 같다
// (KC_A = 0x04 ...) 이므로 0x00~0xFF 를 순서대로 채우면 패스스루가 된다.
#define ROW(r)                                                                 \
  { (r)*16+0x0, (r)*16+0x1, (r)*16+0x2, (r)*16+0x3,                            \
    (r)*16+0x4, (r)*16+0x5, (r)*16+0x6, (r)*16+0x7,                            \
    (r)*16+0x8, (r)*16+0x9, (r)*16+0xA, (r)*16+0xB,                            \
    (r)*16+0xC, (r)*16+0xD, (r)*16+0xE, (r)*16+0xF }

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] =
{
  [0] = {
    ROW(0x0), ROW(0x1), ROW(0x2), ROW(0x3),
    ROW(0x4), ROW(0x5), ROW(0x6), ROW(0x7),
    ROW(0x8), ROW(0x9), ROW(0xA), ROW(0xB),
    ROW(0xC), ROW(0xD), ROW(0xE), ROW(0xF),
  },
};
