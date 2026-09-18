# 001 — Vial 앱 인식 안 됨 · 매크로 / 탭댄스 다중 키 유실

| | |
|---|---|
| 보고 | 2026-09-18, 사용자 (블루투스 매크로패드 + qmk-link, macOS, **Vial 트리**) |
| 상태 | ⬜ 원인 분석 완료 · **미수정 · 실기 재현 안 함** (코드 읽기로 추론) |
| 영향 | vial 트리 전부. 이슈 B 는 via 트리도 같은 경로를 탄다 |

---

## 보고 내용

1. **Vial 데스크톱 앱에서는 안 잡히고 웹(vial.rocks)에서만 된다.**
   케이블을 C-to-A · C-to-C · 데이터용으로 다 바꿔 봐도 안 된다
2. **탭댄스에 Ctrl+Alt+H 같은 3키 조합을 넣으면 동작을 안 한다.**
   - Tapping Term 을 줄이면 **한 번 탭**은 동작한다
   - On hold · On double tap · On tap+hold 는 여전히 안 된다
   - 같은 키보드의 유선판(Vial 펌웨어 직접)에서는 잘 됐다
3. **매크로에 Ctrl+Space(macOS 한영 전환)를 넣으면 아예 동작을 안 한다**

---

## 이슈 A — Vial 데스크톱 앱이 장치를 못 찾는다

### 원인

**USB 시리얼 번호에 Vial 매직 문자열 `vial:f64c2b3c` 가 없다.**

- vial-qmk 는 빌드 때 시리얼을 강제로 박는다

  ```make
  # ~/hdd/git/vial-qmk/builddefs/build_vial.mk:12
  OPT_DEFS += -DVIAL_ENABLE -DNO_DEBUG -DSERIAL_NUMBER=\"vial:f64c2b3c\"
  ```

- Vial 데스크톱 앱(vial-gui, hidapi)의 장치 필터는 대략 이렇다

  ```python
  # vial-gui util.py find_vial_devices()
  if VIAL_SERIAL_NUMBER_MAGIC in dev["serial_number"] and is_rawhid(dev):   # vial 키보드
  elif str(vid * 65536 + pid) in via_stack_json["definitions"] and is_rawhid(dev):  # via-stack
  ```

  시리얼에 매직이 없으면 VID/PID 가 via-stack 목록에 있어야 하는데, 우리 PID
  (`0x5305` / `0x5400`+SLOT) 는 거기 없다 → **목록에 안 뜬다**
- 웹 Vial 은 WebHID 라 시리얼을 못 읽는다. usage page `0xFF60` / usage `0x61` 로 고르고
  프로토콜로 직접 찔러 본다 → **그래서 웹에서만 된다**
- 우리 쪽은 칩 고유 ID 를 시리얼로 쓴다

  ```c
  // firmware/qmk-link/src/hw/driver/usb/usbd_desc.c  usbdDescInit()
  pico_get_unique_board_id_string(serial_str, sizeof(serial_str));
  ```

케이블 · 포트와는 무관하다.

### 수정 계획

- **vial 트리에서만** 시리얼을 `vial:f64c2b3c` 로 시작하게 한다
  - Vial 은 **포함 여부**만 보므로 뒤에 고유 ID 를 붙여 기기 구분을 살린다
    — 예: `vial:f64c2b3c:E66138935F2A7B2C`
  - `serial_str` 크기를 매직 길이만큼 늘린다. `desc_str[32]` (최대 31 글자) 에 들어가는지
    확인한다 — `vial:f64c2b3c:` 14 + 고유 ID 16 = 30 글자로 빠듯하다.
    넘치면 `desc_str` 을 늘린다 (조용히 잘리면 매직은 남아도 기기 구분이 깨진다)
- **계층을 지킨다.** `hw/driver/usb/usbd_desc.c` 가 `VIAL_ENABLE` (QMK 매크로) 을 직접
  보지 않게 한다. `KEY_PROTOCOL_NAME` 처럼 CMake 에서 hw 용 정의를 내려주고
  `hw_def.h` 에서 `HW_USB_SERIAL_PREFIX` 같은 이름으로 받는 쪽을 권한다
  (`_DEF_BOARD_NAME` 이 이미 이 방식이다 — `src/hw/hw_def.h:13`)
- via 트리는 건드리지 않는다. VIA 는 정의 파일 로드 방식이라 시리얼을 안 본다

### 검증

- macOS: `ioreg -p IOUSB -l -w0 | grep -i "USB Serial Number"` 로 `vial:f64c2b3c` 확인
- Vial 데스크톱 앱(macOS · Windows)에서 장치가 목록에 뜨고 키맵 편집 · 저장이 되는가
- 두 대 꽂았을 때 앱이 둘을 따로 보이는가

---

## 이슈 B — 매크로 / 탭댄스에서 연속 리포트가 유실된다

### 원인

**키보드 리포트 보류 칸이 1 칸이라, 엔드포인트가 바쁠 때 연속으로 온 리포트가
서로를 덮어쓴다. 중간 상태가 PC 로 안 나간다.**

```c
// firmware/qmk-link/src/hw/driver/usb/usbd_hid.c  usbdHidSendKeyboard()
usbdHidFlushKeyboard();              // 앞 보류분 flush — busy 면 실패
memcpy(kbd_pending, p_report, 8);    // ★ 실패한 보류분을 덮어쓴다
kbd_pending_valid = true;
return usbdHidFlushKeyboard();
```

주석은 "중간 상태를 건너뛰지 않으려는 것" 이라고 하지만, busy 가 두 번 이어지면
건너뛴다. 엔드포인트 interval 은 1ms (`EP_KBD_INTERVAL`) 라 한 루프 안에서 보낸
두 번째 리포트부터는 거의 항상 busy 다.

QMK 매크로 · 탭댄스는 한 `keyboard_task()` 안에서 리포트를 연달아 보낸다
(`TAP_CODE_DELAY` · `QS_tap_code_delay` 기본 0).

**매크로 Ctrl+Space**

| 순서 | 리포트 | 결과 |
|---|---|---|
| A | Ctrl | 전송 (엔드포인트가 비어 있었다) |
| B | Ctrl+Space | 보류 — A 가 아직 안 빠졌다 |
| C | Ctrl | B 를 덮어쓴다 |
| D | (빈) | C 를 덮어쓴다 → 다음 루프에 전송 |

PC 는 Ctrl 누름 → 뗌만 본다. **Space 는 한 번도 안 나간다.**

**탭댄스 — 보고된 증상과 정확히 맞는다** (`~/hdd/git/vial-qmk/quantum/vial.c`)

| 경우 | 무슨 일이 한 루프에 몰리나 | 결과 |
|---|---|---|
| 탭 (term 200ms) | 뗀 뒤 타임아웃에서 `on_dance_finished`(down) → `on_dance_reset`(up) 연달아 | Ctrl+Alt → **Ctrl+Alt+H** → Ctrl+Alt → 빈. H 가 사라진다 |
| 탭 (term 짧게) | 실제로는 누른 채 타임아웃 → `SINGLE_HOLD`. on_hold 가 비어 있으면 on_tap 으로 대신 down, up 은 손 뗄 때 | 루프 사이에 `usbdHidUpdate()` 가 보류분을 보내서 **된다** |
| On double tap | 두 번째 뗀 뒤 타임아웃에서 down → up 연달아 (탭과 같은 모양) | **유실 — B-1 로 고쳐진다** |
| On double tap 비움 | `vial_keycode_tap()` 직후 `vial_keycode_down()` (vial.c:394) | **유실 — B-1 로 고쳐진다** |
| On hold | 누른 채 타임아웃 → down, 뗄 때 up. 루프가 나뉜다 | 이론상 **지금도 된다** |
| On tap+hold | 두 번째를 누른 채 타임아웃 → down, 뗄 때 up | 이론상 **지금도 된다** |
| On tap+hold 비움 | `vial_keycode_tap()` 직후 down (vial.c:404) | 앞의 탭 유실 — B-1 로 고쳐진다 |

`vial_keycode_down(LCA(KC_H))` 한 번은 리포트 2개(Ctrl+Alt → Ctrl+Alt+H)라 한 칸
보류로도 버틴다. 3개 이상이 한 루프에 몰릴 때부터 깨진다.

일반 키보드의 Vial(ChibiOS) 은 엔드포인트가 빌 때까지 기다리거나 큐에 쌓으므로
이 문제가 없다.

> **On hold · On tap+hold 실패는 이 원인만으로는 설명되지 않는다.**
> 홀드 판정 자체는 된다 — term 을 줄이자 탭이 된 것이 바로 SINGLE_HOLD 경로다.
> 그러면 남는 건 **거기에 넣은 키코드**다. 후보:
> - on_hold 에 **매크로(M0 등)** 를 넣었다 → 매크로가 이슈 B 로 깨진다 (B-1 로 고쳐진다)
> - on_hold 에 레이어 키 · QMK 특수 키 → `action_exec()` 매직 좌표 경로. 별도 확인 필요
> - 사용자가 term 보다 짧게 누르고 있었다 → 탭으로 판정됐다 (홀드가 아니다)
>
> **수정 후 재현 확인하고, 안 되면 사용자에게 해당 탭댄스 설정값을 받는다.**

### 같이 봐야 할 것 ①  NKRO 경로는 보류 칸조차 없다

macOS 는 report protocol 이라 `keymap_config.nkro` 가 켜져 있으면 키 리포트가
`usb_send_nkro()` → `usbdHidSendExtra()` 로 나간다.

```c
// usbd_hid.c  usbdHidSendExtra()
if (tud_hid_n_ready(HID_ITF_EXTRA) != true) return false;   // ★ 그냥 버린다
```

busy 면 **바로 버린다.** 매크로가 더 심하게 깨지고, 뗌이 버려지면 키가 눌린 채 남는다.
마우스 · 컨슈머 · 시스템도 같은 경로다.

### 같이 봐야 할 것 ②  `wait_ms()` 가 `keyboard_task()` 를 재진입시킨다

사용자가 우회하려고 QMK Settings 의 **Tap code delay** 를 올리면 이렇게 된다.

```
keyboard_task()
  → vial_keycode_tap() → qs_wait_ms(QS_tap_code_delay)
    → wait_ms() → delay()                 (port/platforms/wait.c)
      → cliLoopIdle() → qmkUpdate()        (bsp.c, ap.c)
        → keyboard_task()                  ★ 재진입 — 가드 없음 (qmk.c qmkUpdate)
```

매트릭스 스캔 · 탭댄스 상태 · raw HID 처리가 자기 안에서 다시 돈다. 지금 증상의
원인은 아니지만 **이슈 B 를 고칠 때 같이 막는다.** 그 전까지는 사용자에게
Tap code delay 우회를 권하지 않는다.

### 수정 계획

**B-1. 키보드 리포트 FIFO** — `hw/driver/usb/usbd_hid.c`

- `kbd_pending` 한 칸을 **작은 링 버퍼** (예: 16 칸 x 8 B) 로 바꾼다
  - 넣을 때: 큐가 비어 있고 ready 면 바로 보낸다. 아니면 **뒤에 쌓는다**
  - 섀도 비교는 "마지막으로 **큐에 넣은** 리포트" 와 한다 (지금은 "마지막으로 나간 것")
  - `usbdHidUpdate()` 가 ready 일 때마다 **하나씩** 꺼내 보낸다
  - 넘치면: 가장 오래된 것을 버리지 말고 **마지막 칸을 새 것으로 덮는다** —
    최종 상태는 반드시 PC 에 가야 뗌 유실(키 눌림 고착)이 없다. 넘침 횟수를 센다
  - `tud_mount_cb()` / `tud_resume_cb()` 에서 큐를 비운다 (지금 섀도를 비우는 자리)
- `usbd_hid_kbd_stat_t` 에 큐 깊이 · 넘침 카운트를 더해 `key info` 에 보인다
- 인터럽트가 아니라 메인 루프에서만 넣고 빼므로 락은 필요 없다.
  넣는 쪽(`usbdHidSendKeyboard`)과 빼는 쪽(`usbdHidUpdate`) 모두 core0 메인 흐름이다
  — **수정할 때 이 전제를 다시 확인**한다

**B-2. Extra(NKRO · 마우스 · 컨슈머) 도 큐에 태운다** — 같은 파일

- 리포트 ID 별로 길이가 다르므로 `{ id, len, data[최대] }` 칸의 링 버퍼 하나로 둔다
  (NKRO 가 가장 크다 — `report_nkro_t` 크기 확인)
- `usbdHidUpdate()` 에서 키보드 큐와 같이 비운다

**B-3. `qmkUpdate()` 재진입 가드** — `ap/modules/qmk/qmk.c`

- `static bool is_busy` 로 감싼다. 재진입이면 **`keyboard_task()` 와 raw HID 처리는
  건너뛰고** `eeprom_task()` 도 건너뛴다 (플래시 쓰기 중첩 방지)
- USB 처리(`usbUpdate()` · `usbdHidUpdate()`)는 `cliLoopIdle()` 에서 계속 돈다 →
  wait 동안 보류 리포트가 나간다. **이게 Tap code delay 가 원래 의도한 동작이다**
- `linkKbdUpdate()` 도 wait 중에 돌아 QMK 매트릭스를 건드리는지 확인한다.
  건드리면 같은 가드로 막는다 (`ap/modules/link/link_kbd.c`)

**via 트리** 는 B-1 · B-2 가 hw 층이라 자동으로 같이 고쳐진다. B-3 는 `qmk.c` 가
공용이라 역시 같이 적용된다.

### 검증

CLI 로 먼저 원인을 확정하고, 고친 뒤 같은 절차로 확인한다.

1. **수정 전 재현** — Vial 매크로에 `Ctrl+Space` (또는 `{KC_LCTL,KC_SPC}` 탭) 를 넣고
   `key info` 를 본 뒤 매크로 1회 실행, 다시 `key info`
   - 기대(버그): 호출 +4, **보냄 +2**, not ready 증가
2. **수정 후** — 같은 절차에서 **보냄 +4**, 넘침 0
3. 탭댄스 — on_tap `LCA(KC_H)` 로 **term 200ms 기본값** 에서
   탭 · 홀드 · 더블탭 · 탭+홀드 모두 확인 (macOS 에서 Karabiner-EventViewer 로 보면 편하다)
4. NKRO 켠 상태에서 1 · 3 반복
5. QMK Settings 의 Tap code delay 를 10ms 로 올려 1 · 3 반복 — 재진입이 없고 동작한다
6. 회귀 — 일반 타이핑 · 빠른 롤오버 · 미디어키 · VIA 트리 빌드와 동작

---

## 열린 질문

- On hold · On tap+hold 실패는 이슈 B 로 다 설명되지 않는다 (위 표 아래 후보 참고).
  수정 후에도 안 되면 사용자 설정을 받아 따로 본다
- FIFO 깊이 — 가장 긴 Vial 매크로(버퍼 11.7KB) 를 한 번에 흘리면 16 칸으로 모자란다.
  그 경우는 넘침 처리로 최종 상태만 보장되고 중간 글자가 빠진다.
  긴 매크로까지 보장하려면 `dynamic_keymap_macro_send()` 쪽에서 큐가 빌 때까지
  기다리는 방법(재진입 가드 전제)을 검토한다
- 이슈 A 수정 후 Vial 앱이 **PID 전환**(`0x5400`+SLOT 재열거)을 잘 따라가는가
