// Copyright 2022 QMK
// SPDX-License-Identifier: GPL-2.0-or-later

#include "suspend.h"
#include "matrix.h"
#include "action.h"

// TODO: Move to more correct location
__attribute__((weak)) void matrix_power_up(void) {}
__attribute__((weak)) void matrix_power_down(void) {}

/** \brief Run user level Power down
 *
 * FIXME: needs doc
 */
__attribute__((weak)) void suspend_power_down_user(void) {}

/** \brief Run keyboard level Power down
 *
 * FIXME: needs doc
 */
__attribute__((weak)) void suspend_power_down_kb(void) {
    suspend_power_down_user();
}

/** \brief run user level code immediately after wakeup
 *
 * FIXME: needs doc
 */
__attribute__((weak)) void suspend_wakeup_init_user(void) {}

/** \brief run keyboard level code immediately after wakeup
 *
 * FIXME: needs doc
 */
__attribute__((weak)) void suspend_wakeup_init_kb(void) {
    suspend_wakeup_init_user();
}

/** \brief suspend wakeup condition
 *
 * FIXME: needs doc
 */
bool suspend_wakeup_condition(void) {
    matrix_power_up();
    matrix_scan();
    matrix_power_down();
    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        if (matrix_get_row(r)) return true;
    }
    return false;
}

void suspend_power_down(void)
{
  suspend_power_down_quantum();
}

/*
 * ★ 깨어나면 눌린 것을 먼저 비운다.
 *
 *   자는 동안에도 스캔은 계속 돈다. 그런데 리포트는 못 나간다 — 호스트가 안
 *   가져가니 엔드포인트에 실린 채로 남는다. 그래서 잠들 때 눌려 있던 키를 자는
 *   사이에 뗐으면, **호스트는 눌림만 받고 뗌은 못 받는다.**
 *
 *   반대쪽도 있다. 호스트는 서스펜드에서 제 키 상태를 비우는데 우리는 안 비우면,
 *   깨어난 뒤 우리 쪽 리포트가 "안 바뀌었다"로 걸러져 실제로 누르고 있는 키를
 *   호스트가 모르는 채로 간다.
 *
 *   어느 쪽이든 답은 같다 — 깨어나는 자리에서 양쪽을 0 으로 맞춘다. 상류 QMK 의
 *   AVR 포트도 같은 자리에서 같은 것을 한다.
 */
void suspend_wakeup_init(void)
{
  clear_keyboard();
  suspend_wakeup_init_quantum();
}

/*
 * wakeup matrix — QMK 0.33 에서 새로 생긴 API.
 *
 * 서스펜드에서 깨울 때 쓴 키가 그대로 입력으로도 들어가는 것을 막는 장치다.
 * 그 키는 "깨우는 데 썼으니 입력은 아니다" 로 걸러진다.
 *
 * 이 보드는 그 처리를 하지 않는다. USB 버스 전원으로 늘 켜져 있고,
 * 입력은 USB-A 에 꽂힌 키보드에서 오므로 우리가 매트릭스를 재우고 깨우지 않는다.
 * QMK 가 무조건 부르므로 자리만 채운다.
 *
 * (wish-he · hola-mini 가 쓰던 2024-04 판 QMK 에는 이 API 가 없었다)
 */
bool keypress_is_wakeup_key(uint8_t row, uint8_t col)
{
  (void)row;
  (void)col;
  return false;
}

void wakeup_matrix_handle_key_event(uint8_t row, uint8_t col, bool pressed)
{
  (void)row;
  (void)col;
  (void)pressed;
}
