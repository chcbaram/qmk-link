# 06-via — VIA 로 키맵을 편집한다

**상태: ⬜ 미착수** — 구현 상세는 착수할 때 채운다.

## 목표

VIA 웹앱에서 키맵을 편집하고 저장한다. 재부팅해도 유지된다.

## 배경 / 근거

05단계에서 QMK 가 돌아가지만 키맵이 컴파일 시점에 박혀 있다.
편집 가능하게 만들려면 raw HID 프로토콜과 EEPROM 이 필요하다.

04단계에서 raw HID 엔드포인트는 이미 뚫려 있다. 여기서 응답을 채운다.

## 설계

### raw HID

usage page `0xFF60`, usage `0x61`. IN/OUT 각 32바이트.
QMK 의 `via.c` 가 프로토콜을 처리하므로 우리가 할 일은
`raw_hid_receive()` / `raw_hid_send()` 를 TinyUSB 에 연결하는 것뿐이다.

### EEPROM 에뮬레이션 — 이 단계의 핵심 리스크

VIA / Vial 은 키맵을 장치에 저장한다. RP2350 에는 EEPROM 이 없으니 flash 로 흉내낸다.
`hw/driver/eeprom.c` (hola-mini 판을 가져온다) + QMK 의 `eeconfig` / `dynamic_keymap`.

**문제는 XIP 다.** flash 를 지우거나 쓰는 동안에는 flash 에서 코드를 실행할 수 없다.
그런데 03단계에서 **core1 이 PIO USB 를 돌고 있다.** core1 이 그 사이에 flash 코드를
실행하면 그대로 죽는다. USB 호스트 타이밍도 깨진다.

pico-sdk 가 주는 수단:

| 방법 | 내용 |
|---|---|
| `flash_safe_execute()` | 다른 코어를 잠그고(`multicore_lockout`) flash 작업을 한 뒤 푼다. **1순위** |
| `multicore_lockout_victim_init()` | core1 이 잠길 준비를 해 둬야 한다. `usbhCore1Main()` 초기에 부른다 |
| RAM 함수 | flash 루틴을 `__not_in_flash_func` 로 두는 것만으로는 부족하다. core1 도 막아야 한다 |

정해야 할 것:
- core1 을 잠근 동안 USB 호스트가 끊기는 시간이 얼마나 되나 (섹터 지우기 ~수십 ms)
- 그 사이 PC 쪽 device 도 멈춘다. 호스트가 장치를 놓치지 않을 만큼 짧은가
- 키 입력이 없는 타이밍에만 쓰는 식으로 피할 수 있나
- 저장 빈도 — VIA 는 키 하나 바꿀 때마다 쓴다. 캐시해 두고 모아서 쓸지

**착수하면 이것부터 실험한다.** 여기서 막히면 06단계 전체가 막힌다.

### QMK 옵션

```
VIA_ENABLE
RAW_ENABLE
DYNAMIC_KEYMAP_ENABLE
```

### bootloader_jump

VIA 의 "bootloader" 버튼 → `reset_usb_boot(0, 0)`.
02단계에서 만든 것과 같은 함수다. → [usb-stack.md](usb-stack.md#펌웨어-업데이트)

### 빌드 옵션을 VIA 로 뺀다 — custom menu

QMK 는 NKRO · 탭홀드 같은 것을 보통 **컴파일 타임 매크로**로 정한다.
바꾸려면 다시 굽어야 한다. 이 프로젝트는 하드웨어가 고정이라 정작 만지고 싶은 게
전부 그런 옵션이다. **VIA 의 custom menu 로 빼서 런타임에 바꾸게 한다.**

Vial 의 QMK settings 와 같은 효과를 VIA 에서 얻는다. wish-he 가 이 방식을 쓴다:

> VIA 는 정의 JSON 의 menus 를 보고 UI 를 스스로 그린다. 펌웨어는 채널 ID 별로
> 값을 읽고 쓰기만 하면 된다. 그래서 **설정 화면은 웹앱을 포크하지 않아도 된다.**

```
호스트 -> 장치   [0] id_custom_get_value / id_custom_set_value
                 [1] 채널   [2] 값 ID   [3..] 값
```

#### 뺄 후보

| 옵션 | QMK 훅 | 비고 |
|---|---|---|
| NKRO on/off | `keymap_config.nkro` | 이미 런타임이다 (`NK_TOGG`) |
| `TAPPING_TERM` | `get_tapping_term()` | weak 함수를 덮어 eeprom 값을 돌려준다 |
| `HOLD_ON_OTHER_KEY_PRESS` | `get_hold_on_other_key_press()` | 〃 |
| `PERMISSIVE_HOLD` | `get_permissive_hold()` | 〃 |
| `RETRO_TAPPING` | `get_retro_tapping()` | 〃 |
| **볼륨키 변환** | `link/` | HHKB 는 keyboard 페이지 `0x80`/`0x81` 로 보낸다. consumer 로 바꿀지 |
| **패스스루 모드** | `ap.c` | QMK 를 건너뛰고 04단계처럼 그대로 흘린다. 문제 생겼을 때 되돌아갈 곳 |

값은 `EECONFIG_USER_DATA_SIZE` 영역에 둔다 (wish-he 의 `ee_user.h` 방식).

#### 디바운스 메뉴는 두지 않는다

wish-he 의 주석 그대로다:

> 우리는 matrix.c 가 debounce() 를 아예 부르지 않는다. 그 메뉴를 두면
> 아무것도 하지 않는 스위치가 된다.

우리도 같다 — 원본 키보드가 이미 디바운스했다.

### VIA 정의 파일

`info.json` 을 VIA 형식으로 만든다. 16×16 가상 매트릭스를 어떻게 보여줄지가 문제다 —
256키를 그대로 그리면 쓸 수 없으므로, **실제로 쓰는 키만 추린 레이아웃**을 정의한다.

## 구현 항목

- [ ] `hw/driver/eeprom.c` — flash 기반, 듀얼코어 안전
- [ ] `ap/modules/qmk/via/port/via_hid.c` — raw HID ↔ TinyUSB
- [ ] `via.c` / `dynamic_keymap.c` 를 빌드에 포함
- [ ] `info.json` — VIA 레이아웃 정의
- [ ] `bootloader_jump()` 연결
- [ ] CLI `eeprom` 명령 (덤프 / 지우기)

## 완료 판정

1. VIA 웹앱이 장치를 인식한다
2. 키맵을 바꾸면 즉시 반영된다
3. **재부팅 후에도 유지된다**
4. VIA 의 bootloader 버튼으로 BOOTSEL 진입
5. flash 쓰기 중에 core1 의 USB host 가 죽지 않는다

## 열린 질문

| 항목 | 내용 |
|---|---|
| **flash 쓰기 vs core1** | XIP 정지 중 core1 의 `tuh_task()` 가 어떻게 되는지가 최대 리스크다. `flash_safe_execute()` 로 core1 을 잠그거나, 키 입력이 없는 타이밍에만 쓰거나 |
| 레이아웃 | 256키를 다 노출할 수 없다. 어떤 부분집합을 보여줄지 |
| VID / PID | VIA 는 VID/PID 로 정의 파일을 찾는다. `info.json` 과 반드시 일치해야 한다 |
| EEPROM 크기 | dynamic keymap 이 레이어 수 × 256키 × 2바이트다. 레이어를 몇 개로 할지에 따라 flash 사용량이 결정된다 |
