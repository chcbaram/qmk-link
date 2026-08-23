#pragma once

// VIA 트리의 QMK 설정.
//
// 공용 설정은 keyboards/qmk-link/config.h 에 있다 (vial 트리와 같은 것을 본다).
// 여기에는 VIA 고유의 것만 둔다.

#include QMK_BOARD_CONFIG_H


// 탭홀드를 런타임 값으로 뺀다 — VIA 커스텀 메뉴가 값을 준다 (port/via_port.c).
//
// ★ *_PER_KEY 가 없으면 QMK 가 get_*() 를 아예 부르지 않는다.
//   action_tapping.c / action.c 가 컴파일 상수로 굳혀 버려서, 메뉴를 만들어 놓고도
//   슬라이더가 아무 일도 하지 않는 상태가 된다.
//
// ★ vial 트리에는 이것을 두지 않는다.
//   vial.c 가 TAPPING_TERM_PER_KEY 일 때 자기 get_tapping_term() 을 정의해서
//   우리 것과 겹친다. Vial 은 탭홀드 UI 를 자기가 갖고 있으므로 그쪽에 맡긴다.
#define TAPPING_TERM_PER_KEY
#define QUICK_TAP_TERM_PER_KEY
#define HOLD_ON_OTHER_KEY_PRESS_PER_KEY
#define PERMISSIVE_HOLD_PER_KEY
#define RETRO_TAPPING_PER_KEY
