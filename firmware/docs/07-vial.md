# 07-vial — Vial 트리를 추가한다

**상태: ✅ 완료**

## 목표

`-DKEY_PROTOCOL=vial` 로 빌드하면 Vial 앱에서 인식·편집된다.

## 배경 / 근거

06단계가 길을 다 뚫어 놨다. 이 단계는 **트리를 하나 더 만드는 작업**이었다.

### ★ 최대 리스크가 없던 것으로 드러났다

착수 전 열린 질문은 "vial-qmk 의 QMK 베이스가 우리가 이식한 0.33.13 과 달라
`port/` 를 못 쓸 수 있다" 였다. **먼저 쟀고, 사실상 같았다.**

| | vial-qmk |
|---|---|
| `host_driver_t` | 동일 (`send_raw_hid` 6번째 멤버까지) |
| `usb_device_state` | 있음 |
| `quantum/nvm/` | 있음 |
| `platforms/eeprom.h` 시그니처 | 동일 |

그래서 `port/` 929줄이 거의 그대로 옮겨갔다. 갈린 것은 **config.h 와 EEPROM 영역뿐**이다.

## 설계

### 업스트림

```json
"vial-qmk": {
  "url": "https://github.com/vial-kb/vial-qmk.git",
  "rev": "dd43959ae5c08d8a28d38a1acf7b04e86b14a344",
  "sparse": ["quantum", "tmk_core", "platforms"]
}
```

vial-qmk 는 **릴리스 태그가 없다.** `vial` 브랜치로 관리하므로 **커밋 SHA 를 박는다** —
브랜치명을 그대로 두면 재현성이 없다. `fetch_upstream.py` 의 SHA 경로가 이미 이걸 한다
(`git init` + `fetch --depth 1 <sha>`). 받은 크기 10.2MB.

빌드 트리에 따라 원본이 갈린다 (`CMakeLists.txt`):

```cmake
if (KEY_PROTOCOL STREQUAL "vial")
  set(QMK_UPSTREAM_NAME vial-qmk)
else()
  set(QMK_UPSTREAM_NAME qmk_firmware)
endif()
```

### 무엇을 공유하고 무엇을 가르나

```
공유   keyboards/qmk-link/       config.h · keymap.c · layout-kle.json
       ap/modules/link/          HID usage -> 비트맵
       ap/modules/qmk/qmk.c      QMK 기동 · 패스스루 · CLI

가름   ap/modules/qmk/via/       config.h(*_PER_KEY) + port/
       ap/modules/qmk/vial/      config.h(UID · unlock · BUILD_ID) + port/
```

### ★ 세 군데를 놓치면 조용히 망가진다

**① EEPROM 영역** — `port/platforms/eeprom.c`

```c
#define EE_FLASH_BEGIN   HW_FLASH_E2P_VIAL_BEGIN   /* via 는 ..._VIA_BEGIN */
```

via 에서 복사해 온 채로 두면 두 펌웨어가 **같은 영역을 쓰면서 조용히 섞인다.**
eeconfig 매직이 우연히 맞으면 초기화도 안 되고 엉뚱한 키맵이 나온다.

실증 — 같은 오프셋을 두 영역에서 읽은 것이다. Vial 에서 `[0][0][4]` 를 `KC_Z` 로 바꿨다:

```
0x1F0070 : 03 00 04 00 05 ...   VIA 영역  = KC_A   건드리지 않았다
0x1F4070 : 03 00 1D 00 05 ...   Vial 영역 = KC_Z
```

**② 공용 코드에 주소를 박지 않는다** — `qmk.c` 의 `qmk eeprom` 이
`HW_FLASH_E2P_VIA_BEGIN` 을 박아 두고 있었다. vial 에서 엉뚱한 영역을 보여준다.
포트 계층이 `eepromGetBase()` 로 알려주게 바꿨다.

**③ `*_PER_KEY` 는 vial 트리에 두지 않는다** — 아래 참고.

### ★ 탭홀드 — 트리마다 주인이 다르다

`vial.c` 는 `TAPPING_TERM_PER_KEY` 일 때 **자기 `get_tapping_term()` 을 정의한다.**
우리 `via_port.c` 것과 겹친다.

게다가 upstream vial-qmk 에 `#endif` 중첩 실수가 있다:

```c
#ifdef TAPPING_TERM_PER_KEY              // 528
uint16_t get_tapping_term(...) { ... }
uint16_t tap_dance_count(void)  { return VIAL_TAP_DANCE_ENTRIES; }   // ← VIAL_TAP_DANCE_ENABLE 이 필요
tap_dance_action_t* tap_dance_get(...)                               //    인데 가드가 없다
#endif                                   // 553
```

`TAPPING_TERM_PER_KEY` 만 켜고 탭댄스를 안 켜면 컴파일이 깨진다.
Vial 키보드는 보통 둘 다 켜서 아무도 안 밟았던 자리다.

**해결 — 트리마다 주인을 나눈다.**

| | 탭홀드 설정 |
|---|---|
| via | `*_PER_KEY` 를 켜고 **우리 커스텀 메뉴**가 값을 준다 |
| vial | **Vial 이 자기 UI 를 갖고 있다.** `*_PER_KEY` 를 두지 않는다 |

공용 `config.h` 에서 `*_PER_KEY` 를 걷어내고 via 트리의 `config.h` 로 옮겼다.
`via_port.c` 의 훅도 같은 `#ifdef` 로 감쌌다.

### Vial 고유 설정

```c
/* src/ap/modules/qmk/vial/config.h */
#include QMK_BOARD_CONFIG_H          /* 공용 설정 */

#define BUILD_ID ((uint32_t)0x00514C4B)          /* "QLK" */
#define VIAL_KEYBOARD_UID {0x6B, 0xF3, ...}      /* 8바이트, 한 번 정하면 안 바꾼다 */
#define VIAL_UNLOCK_COMBO_ROWS {14, 14}
#define VIAL_UNLOCK_COMBO_COLS { 1,  5}
```

**★ `BUILD_ID` 를 고정한다.** vial-qmk 의 `util/build_id.py` 는 이걸 **빌드마다 난수로**
만든다. 그러면 다시 구울 때마다 EEPROM 이 무효가 되어 **키맵이 초기화된다.**
고정값을 박고 EEPROM 배치를 바꿀 때만 손으로 올린다.
(위 실증에서 `0x1F4065 : 4B 4C 51` 이 이 값이다)

**★ unlock 조합은 좌우 Shift 다.** 좌표가 HID usage 라 물리 위치와 무관하므로,
고르는 기준이 다르다 — **어떤 키보드에나 있는 키**여야 한다.
60% 중에는 오른쪽 Ctrl 이 없는 것도 있다. 좌우 Shift 는 없는 키보드가 사실상 없고
둘을 몇 초씩 함께 누르는 일도 실수로는 잘 일어나지 않는다.

```
LSFT usage 0xE1 -> row 14, col 1
RSFT usage 0xE5 -> row 14, col 5
```

### `matrix_is_on()` 을 우리가 준다

Vial 이 unlock 조합 확인에 쓴다. upstream 은 `quantum/matrix_common.c` 가 주는데
우리는 `matrix.c` 를 통째로 대신하므로 없었다 (링크 에러). 두 트리에 추가했다.

### 정의 파일도 같은 KLE 에서 나온다

`tools/gen_keymap.py` 가 하나의 `layout-kle.json` 에서 넷을 만든다:

```
layout-kle.json ──┬──▶ layout-via.json                        VIA 앱
                  ├──▶ vial.json                              사람이 읽는 원본
                  └──▶ vial_generated_keyboard_definition.h   장치가 내주는 압축본
```

vial-qmk 의 `util/vial_generate_definition.py` 와 같은 일을 한다 (json 최소화 + LZMA).
표준 라이브러리뿐이라 25줄이다. **두 벌을 손으로 관리하면 반드시 어긋난다.**

vial.json 이 VIA 정의와 다른 점은 두 가지뿐이다 — `lighting` 이 필수이고(`none`),
`menus` 를 넣지 않는다(Vial 은 설정 UI 를 자기가 갖고 있다).

**★ Vial 은 이 정의를 장치에서 읽어간다.** 실측 552 B (json 1486 B → LZMA).
09단계에서 키보드마다 다른 정의를 내주려는 근거가 이것이다
→ [09-keyboard-profile.md](09-keyboard-profile.md)

### ★ Vial 앱의 탭은 전부 opt-in 이다

안 켜면 앱에 **Keymap 과 Macros 만 나온다.** 켜면 `vial.h` 가 대응하는
`VIAL_*_ENABLE` 을 자동으로 정의하고 `dynamic_keymap` 이 EEPROM 자리를 잡는다.

| 정의 | 앱의 탭 | 엔트리 |
|---|---|---|
| `QMK_SETTINGS` | QMK Settings | 40 B |
| `TAP_DANCE_ENABLE` | Tap Dance | 16 × 10 B |
| `COMBO_ENABLE` | Combos | 16 × 10 B |
| `KEY_OVERRIDE_ENABLE` | Key Overrides | 8 × 10 B |
| `REPEAT_KEY_ENABLE` | Alt Repeat Key | 8 × 10 B |

**`QMK_SETTINGS` 는 특히 중요하다** — vial 트리는 탭홀드를 Vial 에 맡기기로 했는데
(`*_PER_KEY` 를 두지 않는다), 이걸 안 켜면 탭홀드가 컴파일 상수로 굳어 **아무 데서도
못 바꾼다.** 켜기 전 상태가 정확히 그랬다.

엔트리 개수는 `config.h` 에 명시한다. 안 적으면 `vial.h` 가 EEPROM 크기를 보고
알아서 정하는데, 우리 16KB 면 큰 값을 골라 매크로 버퍼를 잡아먹는다.

#### 켜면서 걸린 것 넷

**① `qmk_settings.c` 는 `CHORDAL_HOLD` · `FLOW_TAP_TERM` 을 전제한다.**
`get_chordal_hold_default()` / `is_flow_tap_key()` 를 가드 없이 부르는데,
그 둘은 `action_tapping.h` 에서 각각 `#ifdef` 안에 있다. 둘 다 켠다
(QMK Settings 가 UI 로 내주는 항목이므로 켜는 게 맞다).

**② `chordal_hold_handedness()` 를 우리가 준다.**
upstream 기본 구현은 `chordal_hold_layout[][]` 표를 읽는데(weak) 그 표가 없다.
**이 매트릭스에는 왼손/오른손이 없다** — 좌표가 usage 라 물리 위치와 무관하다.
`'*'`(어느 쪽도 아님)를 돌려준다. 256칸 표를 만드는 것과 같고 더 싸다.

**③ `TAPPING_TERM_PER_KEY` 를 vial 에서는 켠다 — via 와 반대다.**
upstream `vial.c` 가 `tap_dance_count()` / `tap_dance_get()` 을 이 매크로 안에
넣어 뒀다 (`#endif` 중첩 실수). 안 켜면 `process_tap_dance.c` 가 그 둘을 못 찾는다.
그래서 `get_tapping_term()` 의 주인이 `vial.c` 가 되고, 우리 `via_port.c` 의 훅은
`#ifndef VIAL_ENABLE` 로 빠진다.

**④ 잔챙이 소스 둘.** `process_combo.c` 가 `biton()`(`bitwise.c`),
`qmk_settings.c` 가 `set_autoshift_timeout()`(`process_auto_shift.c`) 을 부른다.
Auto Shift 는 기본값이 꺼짐이라(`QS.auto_shift = 0`) 켜도 동작이 바뀌지 않는다.

### 이름으로 구분한다

```
build/src/qmk-link-via.uf2        USB 제품 이름 "QMK-LINK VIA"
build-vial/src/qmk-link-vial.uf2  USB 제품 이름 "QMK-LINK VIAL"
```

`PRJ_NAME` 에 프로토콜을 붙이고, `KEY_PROTOCOL_NAME` 을 컴파일 정의로 넘겨
`_DEF_BOARD_NAME` 과 정의 파일의 `name` 을 갈랐다.
두 펌웨어를 번갈아 구울 때 **지금 뭐가 올라가 있는지 OS 에서 바로 보인다.**

## 구현 항목

- [x] `upstream.json` 에 `vial-qmk` (커밋 SHA 고정)
- [x] `CMakeLists.txt` 가 `KEY_PROTOCOL` 로 원본을 고른다
- [x] `ap/modules/qmk/vial/` 트리 (via 에서 복사 + 세 군데 수정)
- [x] via 트리에도 `config.h` 신설 (`*_PER_KEY` 를 공용에서 옮김)
- [x] `gen_keymap.py` 가 `vial.json` + 정의 헤더까지 생성
- [x] `matrix_is_on()`
- [x] `eepromGetBase()` — 공용 코드에서 주소 하드코딩 제거
- [x] 산출물 · USB 이름 분리

## 완료 판정

| # | 항목 | 결과 |
|---|---|---|
| 1 | `-DKEY_PROTOCOL=vial` 빌드 | ✅ FLASH 114,224 B / RAM 154,812 B |
| 2 | Vial 프로토콜 응답 | ✅ 버전 6, UID 일치 |
| 3 | 장치가 정의를 내준다 | ✅ 552 B, 압축 풀어 153키 확인 |
| 4 | 키맵 편집·저장·재부팅 유지 | ✅ `[0][0][4]` 0x001D 유지 |
| 5 | EEPROM 이 Vial 영역 | ✅ `0x1F4000`, VIA 영역은 그대로 |
| 6 | via 로 되돌려도 동작 | ✅ FLASH 111,256 B |
| 7 | 산출물 이름 구분 | ✅ `qmk-link-via.uf2` / `qmk-link-vial.uf2` |
| 8 | Vial 앱 인식 | ✅ 웹 Vial 에서 `QMK-LINK VIAL`, 8레이어 |
| 9 | 기능 탭 | ✅ tap dance 16 / combo 16 / key override 8 / alt repeat 8 / QMK Settings 15항목 / 매크로 16개(버퍼 11,695 B) |

## 열린 질문

| 항목 | 내용 |
|---|---|
| **구버전 Vial 데스크톱 앱** | 웹 Vial 은 되는데 로컬 0.7.5(Python 3.6.8 / Qt 5.9.3)는 장치를 못 잡는다. 펌웨어 쪽 근거는 다 확인했다 — 프로토콜 6은 도입 이래 바뀐 적이 없고(이력에 커밋 1개), `vial.json` 은 vial-qmk 예제 57개 중 28개와 키 구성이 완전히 같다. **다른 Vial 키보드를 그 앱에 꽂아 보면 앱 문제인지 갈린다** |
| ~~Vial 의 QMK Settings~~ ✅ | 켰다. 15항목 |
| 패스스루 | vial 트리에는 커스텀 메뉴가 없어 UI 가 없다. CLI 로만 조작한다 |
| 두 트리의 `port/` 중복 | 실제로 갈린 것은 `config.h`·`eeprom.c` 두 줄뿐이다. 공유를 검토할 만하다 (지금은 중복을 감수) |
