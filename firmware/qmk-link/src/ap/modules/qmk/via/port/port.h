#pragma once

/*
 * QMK 포트 계층 — 이 보드가 QMK 에 채워 넣는 것들.
 *
 * ★ 이 보드에는 물리 매트릭스가 없다.
 *
 *   USB-A 에 꽂힌 키보드가 보낸 HID 리포트를 ap/modules/link 가 비트맵으로 바꾸고,
 *   port/matrix.c 가 그걸 읽어 QMK 에 넘긴다 (usage 를 그대로 좌표로 쓴다).
 *
 *     - 디바운스를 쓰지 않는다. 원본 키보드가 이미 했다. 여기서 또 하면 지연만 는다.
 *       그래서 DEBOUNCE_TYPE 은 none 이다.
 */

#include "via_hid.h"
#include "via.h"
#include "eeconfig.h"



