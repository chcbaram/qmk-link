# 001 — Vial 앱 인식 안 됨 · 매크로 / 탭댄스 다중 키 유실

| | |
|---|---|
| 보고 | 2026-09-18, 사용자 (블루투스 매크로패드 + qmk-link, macOS, **Vial 트리**) |
| 상태 | 🟨 **코드 수정 완료 · 실기 확인 대기** (2026-09-19) |
| 영향 | vial 트리 전부. 이슈 B 는 via 트리도 같은 경로를 탄다 |

| 수정 | 커밋 |
|---|---|
| B-1 · B-2 리포트 큐 | `0d070bc` 못 보낸 HID 리포트를 큐에 쌓는다 |
| B-3 재진입 가드 | `30f4358` qmkUpdate() 재진입을 막는다 |
| A 시리얼 매직 | `8535295` vial 트리 시리얼에 Vial 매직을 붙인다 |

**아직 실기에서 재현도 확인도 안 했다.** 아래 "검증" 절차를 그대로 밟아야 닫힌다.

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

### 수정한 것 — `8535295`

- `hw_def.h` 에 `HW_USB_SERIAL_PREFIX = "vial:f64c2b3c:"`.
  CMake 가 `KEY_PROTOCOL_VIAL` 을 내려줘서 vial 트리에서만 켜진다
  (`KEY_PROTOCOL_NAME` 과 같은 방식 — `usbd_desc.c` 가 QMK 의 `VIAL_ENABLE` 을
  직접 보면 계층이 깨진다)
- 매직 뒤에 칩 고유 ID 를 그대로 붙인다 — Vial 은 **포함 여부**만 보므로
  기기 구분이 살아 있다. `vial:f64c2b3c:E66138935F2A7B2C`
- **길이가 빠듯한 게 맞았다.** 14 + 16 = 30 자, string descriptor 한계는 31 자.
  잘리면 매직은 남고 고유 ID 만 깨져서 조용히 어긋난다 → `_Static_assert` 로
  빌드를 세운다. 접두어를 늘리려면 `desc_str` 부터 늘려야 한다
- 확인: vial 의 `serial_str` 31 B, via 는 17 B 그대로. via 바이너리에 매직 없음

### 남은 위험

- Vial 앱이 시리얼을 **기기 식별 키**로도 쓰면, 고유 ID 부분은 절대 바꾸면 안 된다.
  PID 는 SLOT 따라 바뀌지만 시리얼은 안 바뀌므로 지금 구성은 안전하다

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

### ★ 덮어쓰기 말고 실패 모드가 하나 더 있었다 — 키가 눌린 채 남는 진짜 원인

수정 검토 중에 찾았다. 섀도를 **"마지막으로 나간 리포트"** 와 비교하는 게 문제다.

```
A (빈)  전송,      섀도 = 빈
B  X    busy → 보류
C (빈)  섀도와 같다 → ★ 조용히 버림 (same_cnt++). 보류 B 는 그대로다
        루프 끝 → 뒤늦게 B 가 나간다
```

PC 는 X 를 **누른 채로** 남는다. QMK 는 뗌을 이미 보냈다고 알고 다시 안 보내므로
다음 키 이벤트가 올 때까지 고착이다. 같은 이유로 매크로 안의 반복 키(`aa`)는
두 번째가 통째로 사라진다.

덮어쓰기는 "중간 키가 안 나간다", 이건 "키가 눌린 채 남는다" 로 증상이 다르다.
고치는 방법은 하나다 — **섀도를 "마지막으로 큐에 넣은 것" 과 비교한다.**

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

### 수정한 것

**B-1 · B-2. 리포트 큐** — `0d070bc`, `hw/driver/usb/usbd_hid.c`

- 키보드 16칸(8 B), IF1(Extra) 16칸(`{id, len, data[31]}`) 링 버퍼.
  `usbdHidUpdate()` 가 ready 일 때마다 **하나씩** 꺼낸다
  (한 번에 여러 개는 의미가 없다 — 하나 걸면 호스트가 가져갈 때까지 busy 다)
- **섀도는 "마지막으로 큐에 넣은 것"** 과 비교한다 (위 ★ 절)
- Extra 칸 크기 = 31 B. `report_nkro_t` 가 32 B(`id + mods + bits[30]`)이고
  ID 는 떼고 나간다. 여기가 가장 크다
- `usbdHidSendMouse()` 도 `usbdHidSendExtra()` 를 타게 했다 — 컨슈머 · NKRO 와
  순서가 지켜지고 busy 에 안 버려진다
- **넘침 — 덮어쓰기 전에 짧게 기다린다.** 같은 파일의 `usbdHidSendRaw()` 가
  쓰던 방식 그대로 `tud_task()` 만 돌리며 10ms 기다린다
  (`delay()` 는 `cliLoopIdle()` 을 돌려서 재진입이라 못 쓴다).
  붙어 있지 않으면(`tud_mounted` false · suspend) 기다리지 않는다 — 리포트마다
  10ms 를 까먹으면 키보드가 멎은 것처럼 보인다.
  그래도 안 빠지면 **마지막 칸을 덮는다.** 최종 상태는 반드시 나가야 한다
- `tud_mount_cb()` 에서 섀도와 큐를 버린다.
  **`tud_resume_cb()` 에서는 큐를 비우지 않는다** — suspend 는 끊긴 게 아니라
  호스트가 우리 키 상태를 기억하고 있고, 큐에 **뗌**이 들어 있을 수 있다.
  비우면 키가 눌린 채 남는다. 섀도만 무효화한다
- `key info` 에 큐 깊이 · 최대 · 기다림 · 넘침을 보인다. IF1 통계도 새로 나온다
- 넣는 쪽(`usbdHidSendKeyboard`)도 빼는 쪽(`usbdHidUpdate`)도 core0 메인 흐름이라
  락이 없다 — `tud_task()` 는 core0, PIO USB 호스트만 core1 이다 (확인함)

**B-3. `qmkUpdate()` 재진입 가드** — `30f4358`, `ap/modules/qmk/qmk.c`

- `is_busy` 로 감싼다. `keyboard_task()` · raw HID · `eeprom_task()` 를 건너뛴다
- **`linkKbdApplySlot()` 도 막았다** (`ap/ap.c`). 원래 계획에는 `linkKbdUpdate()` 만
  있었는데, 실제로 위험한 건 그 다음 줄이다 —
  `linkKbdApplySlot()` → `qmkSetProfile()` → **`clear_keyboard()` + `layer_clear()`**.
  판정 중인 탭댄스 키와 레이어가 통째로 날아간다.
  (`usbSetProductId()` 자체는 안전하다 — 재열거는 `usbUpdate()` 가 조용해질 때까지 미룬다)
- `linkKbdUpdate()` 는 그대로 돌게 뒀다. 매트릭스 비트맵만 건드리고 다음 스캔이
  받으므로 무해하고, 기다리는 동안 USB 호스트 큐를 비워 주는 게 낫다
- USB 처리(`usbUpdate()` · `usbdHidUpdate()`)는 계속 돈다 → wait 동안 큐가 나간다.
  **이게 Tap code delay 가 원래 의도한 동작이다**

**via 트리** 는 B-1 · B-2 가 hw 층, B-3 가 공용 `qmk.c` 라 같이 고쳐졌다.

### 참고 — upstream QMK 는 어떻게 하나

같은 문제를 일반 키보드의 QMK 도 당연히 겪는다. 두 갈래로 나뉜다.
(`~/hdd/git/vial-qmk` 에서 확인)

| | 방식 | 큐 | 꽉 차면 |
|---|---|---|---|
| **AVR (LUFA)** `tmk_core/protocol/lufa/lufa.c:97` | **큐가 없다.** 엔드포인트가 빌 때까지 `_delay_us(40)` x 255 = **약 10ms** 돌고, 그래도 안 되면 **버린다** | 없음 | 버림 |
| **ARM (ChibiOS)** `tmk_core/protocol/chibios/usb_driver.c` `usb_endpoint_in_send()` | 엔드포인트마다 출력 큐. 꽉 차면 RTOS 스레드가 **블로킹**한다 (`TIME_MS2I(100)`) | **4칸** (`USB_DEFAULT_BUFFER_CAPACITY`) | 100ms 뒤 **큐를 통째로 리셋**하고 새 것만 넣는다 |
| **우리** | 큐 + `tud_task()` 를 돌리며 대기 (블로킹할 스레드가 없다) | **16칸** | 10ms 뒤 **마지막 칸만 덮는다** |

읽을 것 세 가지 —

1. **"엔드포인트가 빌 때까지 기다린다" 는 upstream 도 똑같이 한다.**
   AVR 은 아예 큐 없이 10ms 를 기다린다. 우리 10ms 는 근거 없이 고른 값이 아니라
   **AVR QMK 와 같은 값**이다
2. **RTOS 가 있으면 블로킹하면 되지만 우리는 못 한다.** ChibiOS 는 스레드를 재우면
   USB 인터럽트가 알아서 큐를 빼 간다. 우리는 협조적 단일 루프라 `tud_task()` 를
   직접 돌려야 같은 일이 벌어진다. `delay()` 는 `cliLoopIdle()` 을 타서 재진입이 되므로
   못 쓴다 — 여기가 이 보드만의 차이다
3. **넘침 정책은 우리 쪽이 더 보수적이다.** ChibiOS 는 밀린 걸 **전부 버리고**
   새 것만 남긴다. 우리는 오래된 15칸을 지키고 **마지막 칸만** 덮는다.
   최종 상태도 지켜지고 중간 상태도 최대한 남는다

즉 "다른 Vial 키보드는 어떻게 되나" 의 답은 — **애초에 이 버그가 없다.**
큐가 있거나(ARM), 큐는 없어도 엔드포인트를 기다리기 때문이다(AVR).
우리는 기다리지도 쌓지도 않고 **한 칸을 덮어쓰고 있었다.** 우리만의 버그였다.

### 빌드 영향

| | FLASH | RAM |
|---|---|---|
| via | 122,764 → 124,124 B | 238 → 239,944 B |
| vial | 141,164 → 142,564 B | 258 → 259,820 B |

### 검증 ① 시뮬레이션 — ✅ 통과 (2026-09-19)

[`firmware/qmk-link/test/`](../../qmk-link/test/README.md) 에 넣었다.
**펌웨어의 `usbd_hid.c` 를 호스트에서 그대로 컴파일해서** 돌린다 —
TinyUSB 와 시간만 흉내낸다 (엔드포인트 1ms, `tud_task()` 50us).

```bash
cd firmware/qmk-link/test
make                    # 지금 소스  -> 전부 통과
make rev REV=269f452    # 수정 전    -> 1~4 실패
```

**수정 전 코드에서 보고된 증상이 그대로 재현됐다.**

| 시나리오 | 보낸 것 | 수정 전 PC 가 본 것 | 수정 후 |
|---|---|---|---|
| 매크로 `Ctrl+Space` | `[C][C_][C][-]` | **`[C][-]`** — Space 가 안 나간다 | `[C][C_][C][-]` |
| 매크로 반복 키 `aa` | `[a][-][a][-]` | **`[a][-]`** — 두 번째 a 가 사라진다 | `[a][-][a][-]` |
| 탭댄스 `LCA(KC_H)` 탭 | `[CA][CAh][CA][-]` | **`[CA][-]`** — H 가 안 나간다 | `[CA][CAh][CA][-]` |
| 긴 매크로 40자(80 리포트) | 80개 | **2개** | 80개 전부 |
| 호스트가 멈춘 동안 80 리포트 | 80개 | — | 16개, **마지막이 뗌** (고착 없음) |

보고 2번("탭댄스에 Ctrl+Alt+H 를 넣으면 동작을 안 한다")과 3번("매크로에
Ctrl+Space 를 넣으면 아예 동작을 안 한다")이 **원인 그대로 재현됐다.**
이슈 B 의 분석은 이것으로 확정이다.

### 검증 ② 실기 — 🟨 일부 통과 (2026-09-19)

| | 결과 |
|---|---|
| **이슈 A — 시리얼** | ✅ `vial:f64c2b3c:A68CB0CBBEF05A56`. 30자, 안 잘렸다. 제품명도 `QMK-LINK VIAL` |
| **이슈 A — Vial 앱 인식** | ⬜ 사용자가 확인한다 |
| **가상 키 주입 · 기록** | ✅ `key sim` 을 새로 넣었다 (아래) |
| **한 task 에 리포트 2개** | ✅ 실기에서 확인 — 아래 `#2`·`#3` 이 같은 +50ms |
| **매크로 · 탭댄스 종단간** | ⬜ Vial 앱으로 설정해야 한다 |
| **Tap code delay 재진입** | ⬜ 같이 확인한다 |

`qmk info` 의 task cnt 가 정상 속도로 돈다 (재진입 가드가 루프를 세우지 않는다).

**보드에서 하는 법 — `key sim`**

```
key sim tap <usage> [hold_ms]     가상 키 하나
key sim report <8바이트> [..]     mods 포함 boot 리포트를 그대로
```

호스트로는 **안 내보낸다. 기록만 한다** — 주입한 키는 진짜 키와 구별이 안 되므로
그대로 PC 에 입력된다. 명령 하나가 곧 실험 하나이고 끝나면 스스로 원복한다.

```
cli# key sim report 0x01 0 0x2C          (Ctrl+Space 를 누른 것처럼)
  #   시각  단계 IF   리포트
  0   +0      요청 IF0  mods 00  keys 2C ...
  1   +1      요청 IF0  mods 01  keys 2C ...
  2   +50     요청 IF0  mods 01  keys 00 ...
  3   +50     요청 IF0  mods 00  keys 00 ...
```

`#2`·`#3` 이 **같은 +50ms** 다. 한 번의 `keyboard_task()` 안에서 두 리포트가
나간다 — 이슈 B 가 깨지던 바로 그 조건이 실기에서 확인됐다.

**남은 절차** — Vial 앱에서 예전처럼 매크로(`Ctrl+Space`)·탭댄스(`LCA(KC_H)`)를
넣고, 그 자리의 usage 로 `key sim tap` 한 뒤 기록을 본다. 중간 리포트가
전부 보이면 된다. 그 다음 실제로 눌러 PC 입력을 확인한다.

### 부수 관찰 — `key info` 의 `IF1 not ready` 가 크게 보인다

부팅 직후 `not ready` 가 11만을 넘는다. **고장이 아니다.**

QMK 가 켜질 때 Extra 리포트 3개를 보내는데 그중 2개가 호스트 열거 전이라
큐에 남고, 메인 루프가 도는 동안 매 바퀴 `not ready` 가 오른다.
열거가 끝나면 `tud_mount_cb()` 가 큐를 비워 (옛 호스트에게 보내려던 것이라
의미가 없다) 카운터가 멈춘다. 그 구간에는 `tud_mounted()` 가 false 라
큐가 꽉 차도 10ms 대기에 안 걸린다 — B-1 에 넣은 가드가 여기서 일한다.

`not ready` 는 리포트마다가 아니라 **루프마다** 오르는 값이다. 읽을 때 주의한다.

1. **이슈 A** — vial 을 구운 뒤
   `ioreg -p IOUSB -l -w0 | grep -i "USB Serial Number"` 에
   `vial:f64c2b3c:<고유ID>` 가 보이는가.
   Vial **데스크톱 앱**(macOS · Windows)에 장치가 뜨고 키맵 편집 · 저장이 되는가.
   두 대 꽂으면 앱이 둘을 따로 보는가
2. **매크로** — Vial 매크로에 `Ctrl+Space` 를 넣고 `key info` → 1회 실행 → `key info`
   - 수정 전(버그): 호출 +4, **보냄 +2**, not ready 증가
   - 수정 후: **보냄 +4**, 넘침 0
3. **반복 키** — 매크로에 `aa` 를 넣고 실행. `a` 가 **두 번** 나오는가
   (위 ★ 절의 실패 모드다. 수정 전에는 한 번만 나온다)
4. **탭댄스** — on_tap `LCA(KC_H)` 로 **term 200ms 기본값** 에서
   탭 · 홀드 · 더블탭 · 탭+홀드 모두 확인
   (macOS 는 Karabiner-EventViewer 로 보면 편하다)
5. **NKRO** — 켠 상태에서 2 · 3 · 4 반복. IF1 통계의 넘침이 0 인가
6. **Tap code delay** — QMK Settings 에서 10ms 로 올려 2 · 4 반복.
   재진입 없이 동작하는가. `qmk info` 의 task cnt 가 정상 속도로 도는가
7. **회귀** — 일반 타이핑 · 빠른 롤오버 · 미디어키 · 키보드 뺐다 꽂기(SLOT 전환) ·
   VIA 트리 빌드와 동작

---

## 열린 질문

- On hold · On tap+hold 실패는 이슈 B 로 다 설명되지 않는다 (위 표 아래 후보 참고).
  수정 후에도 안 되면 사용자 설정을 받아 따로 본다
- ~~FIFO 깊이 — 긴 매크로(버퍼 11.7KB)~~ → **답이 나왔다.** 큐가 꽉 차면
  `usbdHidSendKeyboard()` 안에서 `tud_task()` 를 돌리며 기다리므로 길이에 관계없이
  흘러간다. `dynamic_keymap_macro_send()` 를 건드릴 필요가 없어 계층도 안 깨진다.
  다만 엔드포인트가 1ms 마다 하나씩 가져가므로 **긴 매크로는 원래 오래 걸린다**
  (글자당 리포트 2개 → 100자면 0.2초). 이건 USB 의 성질이지 버그가 아니다.
  10ms 안에 안 빠지는 건 호스트가 안 가져갈 때뿐이고, 그때는 넘침 카운트가 오른다
- 이슈 A 수정 후 Vial 앱이 **PID 전환**(`0x5400`+SLOT 재열거)을 잘 따라가는가
- `STR_ID_RAW` 가 vial 트리에서도 `"QMK-LINK VIA"` 로 나간다. 앱은 usage page 로
  고르므로 동작에는 영향이 없지만 이름이 어긋난다 — 따로 고칠 것
