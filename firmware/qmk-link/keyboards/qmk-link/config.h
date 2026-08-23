#pragma once

// qmk-link 의 QMK 설정.
//
// 이 프로젝트는 물리 매트릭스가 없다. USB-A 에 꽂힌 키보드가 보낸 HID usage 를
// 그대로 좌표로 쓴다 (ap/modules/link).
//
//   row = usage >> 4,  col = usage & 0x0F   ->  16 x 16 = 256 키
//
// HID usage 는 0x00~0xFF 라 이 크기면 전부 담긴다.

#define KBD_NAME                    _DEF_BOARD_NAME

#define USB_VID                     HW_USB_VID
#define USB_PID                     HW_USB_PID

#define MATRIX_ROWS                 16
#define MATRIX_COLS                 16

// 디바운스는 쓰지 않는다. 원본 키보드가 이미 했고,
// 우리 matrix_scan() 은 샘플링이 아니라 마지막 리포트를 읽을 뿐이다.
#define DEBOUNCE                    0

#define DYNAMIC_KEYMAP_LAYER_COUNT  8

// ★ 탭홀드 옵션은 트리마다 다르다 — 여기 두지 않는다.
//
//   via  : *_PER_KEY 를 켜고 우리 커스텀 메뉴가 값을 준다 (port/via_port.c)
//   vial : Vial 이 자기 UI 를 갖고 있다. *_PER_KEY 를 켜면 vial.c 의
//          get_tapping_term() 과 우리 것이 겹친다
//
//   TAPPING_TERM 기본값만 공용으로 둔다.
#define TAPPING_TERM                200

// eeprom — 내장 플래시 0x1F0000 부터 16KB (hw_def.h 의 HW_FLASH_E2P_VIA_*)
//
//   dynamic keymap  8레이어 x 16 x 16 x 2B = 4096B
//   나머지는 eeconfig · VIA user data · 매크로 버퍼가 나눠 쓴다
//
// Vial 트리는 같은 크기를 0x1F4000 에 따로 갖는다. 섞이면 안 된다.
// VIA 앱의 Key Tester > Test Matrix 를 살린다.
//
// ★ 이게 없으면 upstream via.c 가 매트릭스 조회에 무조건 0 을 넣는다.
//   (id_switch_matrix_state 의 #else 가지 — 실측으로 확인했다)
//   기능이 있는 것처럼 보이는데 영영 아무것도 안 뜬다.
//
// ★ 이 프로젝트에서는 특히 쓸모가 크다.
//
//   꽂은 키보드가 어떤 HID usage 를 보내는지 VIA 에서 바로 보인다.
//   좌표가 곧 usage 라 눌린 키가 배열에서 그대로 반짝인다.
//   이게 없으면 CLI `qmk matrix` 로만 알 수 있어서, 시리얼 터미널을 붙이지
//   않는 사용자는 방법이 없다.
//
// 대가는 raw HID 를 여는 앱이 눌린 키를 읽을 수 있다는 것이다.
// QMK 가 기본으로 막아 둔 이유가 그것이다. 이 보드는 그 위험보다
// "어떤 usage 가 오는지 알아야 한다" 는 쪽이 크다고 보고 켠다.
#define VIA_INSECURE

// VIA 앱의 "Reset EEPROM" 버튼을 살린다.
// 키맵이 꼬였을 때 CLI 없이 되돌릴 수 있는 길이 하나 더 생긴다.
#define VIA_EEPROM_ALLOW_RESET

#define EECONFIG_USER_DATA_SIZE     64
#define TOTAL_EEPROM_BYTE_COUNT     16384

