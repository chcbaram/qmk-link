# 05-qmk — QMK 를 얹는다

**상태: ⬜ 미착수** — 구현 상세는 착수할 때 채운다.

## 목표

04단계의 패스스루 자리에 **QMK 키 처리**를 넣는다.
레이어 · 모디파이어 · 탭홀드가 동작한다.

## 배경 / 근거

여기까지 오면 하드웨어와 USB 리스크는 다 걷혀 있다. 남은 건 소프트웨어다.

**입력이 매트릭스가 아니라 HID report 라는 점**이 이 프로젝트의 핵심이자
일반 QMK 포팅과 다른 부분이다.

## 설계

### 업스트림을 저장소에 넣지 않는다

QMK 소스는 `firm-sdk/upstream/qmk_firmware` 에 받아 쓰고,
**우리 저장소에는 `port/` 레이어와 키보드 정의만 커밋한다.**

```bash
python3 firmware/firm-sdk/tools/fetch_upstream.py
```

`upstream.json` 이 리비전을 고정한다.

| | 리포지토리 | 값 |
|---|---|---|
| QMK | `github.com/qmk/qmk_firmware` | 릴리스 태그 `0.33.13` |

받는 방식:

```bash
git clone --depth 1 --filter=blob:none --sparse --branch 0.33.13 <url> <dir>
git -C <dir> sparse-checkout set quantum
```

전체 히스토리는 524MB 지만 `quantum/` 만 받으면 5MB 안팎이다.
hola-mini 가 QMK 에서 실제로 쓴 것도 `quantum/` 뿐이었다 (519 파일 / 3.5MB).

→ 근거는 [00-context.md](00-context.md#확정된-결정과-이유)

### 트리

```
src/ap/modules/
├── link/                HID report → 키 상태 비트맵 (QMK 무관, 공통)
└── qmk/
    └── via/
        ├── CMakeLists.txt    upstream 에서 QMK_SRC_FILES / QMK_INC_DIR 구성
        ├── config.h  keymap.c  info.json  version.h
        └── port/             platforms · protocol · matrix.c
```

`link/` 는 QMK API 를 만지지 않는 순수 로직이라 07단계의 vial 트리와 공유한다.
QMK API 를 만지는 `matrix.c` 는 각 트리의 `port/` 안에 둬서 의존을 한 방향으로만 유지한다.

### 가상 매트릭스

USB HID usage 코드(0x00~0xFF)를 그대로 좌표로 쓴다.

```
MATRIX_ROWS = 16
MATRIX_COLS = 16
row = usage >> 4,  col = usage & 0x0F
```

`link/` 가 256비트 비트맵을 채우고, `port/matrix.c` 의 `matrix_scan()` 이 그걸 읽는다.
keymap 도 이 좌표계로 쓴다.

### 포트 레이어

hola-mini 의 `src/ap/modules/qmk/port/` 를 가져온다
(`platforms/`, `protocol/`, `bootloader.c`, `eeprom.c`, `timer.c`, `wait.c`, `suspend.c`).

### 출력은 `host_driver_t` 로 나간다 — 04단계 패스스루를 대체한다

지금은 `ap.c` 가 `usbdHidSendKeyboard()` 를 직접 부른다. QMK 가 올라가면 **그러면 안 된다.**
QMK 의 출력은 전부 `host_driver_t` 의 함수 포인터를 지난다.
`wish-he` 의 `port/driver_usb.c` 가 참고 구현이다.

```c
host_driver_t usb_driver = {
  usb_keyboard_leds,    // 호스트가 준 LED 상태 (usbdHidGetLed)
  usb_send_keyboard,    // report_keyboard_t = 부트 8바이트 그대로
  usb_send_nkro,        // report_nkro_t  = { id, mods, bits[30] }
  usb_send_mouse,       // report_mouse_t = { id, buttons, x, y, v, h }
  usb_send_extra,       // report_extra_t = { id, usage }  시스템/컨슈머 공용
};
...
host_set_driver(&usb_driver);
```

**04단계 descriptor 와 이미 맞는다 (확인함).** `host.c` 가
`report->report_id = REPORT_ID_NKRO` (=6) 를 채우고 `NKRO_REPORT_BITS` 가 30 이라,
IF1 에 넣어 둔 NKRO 리포트 ID 6 / 30바이트 비트맵과 일치한다.
mouse 2 / system 3 / consumer 4 도 그대로다.

#### `keyboard_protocol_get()` 을 틀리면 BIOS 에서 키가 안 먹는다

wish-he 에 실측 기록이 있다:

> 예전에는 늘 1 을 돌려줬다. 그러면 QMK 가 NKRO 만 보내서 부트 프로토콜만 아는
> BIOS · 부트로더에서 키가 하나도 안 먹는다 — 실측으로 KBD sent 1 / EXK 6440 이었다.

**호스트가 SET_PROTOCOL 로 정한 값을 그대로 돌려줘야 한다.**
TinyUSB 는 `tud_hid_n_get_protocol(HID_ITF_KEYBOARD)` 로 준다.

#### `host_can_send_nkro()`

우리도 IF0(부트) 과 IF1(NKRO) 이 따로 있으므로, 부트 프로토콜 여부와 무관하게
**IF1 이 configured 인지**로 판단한다. BIOS 는 IF0 을, OS 는 IF1 을 본다.

#### 전송 정책 — "바뀔 때만 싣는다"

QMK 는 같은 리포트를 여러 번 보낼 수 있다 (레이어 처리 중간 상태 등).
그때마다 전송을 걸면 옛 리포트가 먼저 나가느라 **지연이 오히려 늘어난다.**
섀도 복사본과 비교해 같으면 아무것도 하지 않는다.

## 구현 항목

- [ ] `firm-sdk/upstream.json`
- [ ] `firm-sdk/tools/fetch_upstream.py` — shallow + partial + sparse, `--update`, `--check`
- [ ] CMake configure 단계에서 `fetch_upstream.py --check` 자동 호출
- [ ] **`0.33.13` 기준으로 hola-mini 포트 레이어 컴파일 가능 여부 확인** ← 가장 먼저
- [ ] `ap/modules/qmk/via/port/` — platforms · protocol · matrix.c
- [ ] `ap/modules/qmk/via/port/driver_usb.c` — `host_driver_t` 구현.
      `ap.c` 의 직접 송신(04단계 패스스루)을 이걸로 대체한다
- [ ] `keyboard_protocol_get()` — `tud_hid_n_get_protocol()` 을 그대로 돌려준다
- [ ] `ap/modules/qmk/via/{config.h, keymap.c, info.json, version.h}`
- [ ] `ap/modules/link/` — HID report → 비트맵 (04단계 패스스루를 대체)
- [ ] `ap/modules/qmk/via/CMakeLists.txt`
- [ ] 루트 CMakeLists 의 `KEY_PROTOCOL` 옵션 연결

## 완료 판정

1. `fetch_upstream.py` 로 받은 `upstream/qmk_firmware` 크기가 10MB 미만
2. 빌드가 통과하고 FLASH 사용량이 2MB 안에 든다
3. USB-A 키보드로 타이핑하면 keymap 대로 PC 에 입력된다
4. 레이어 키가 동작한다
5. 탭홀드(`LT`, `MT`)가 동작한다
6. 모디파이어 조합이 정상

## 열린 질문

| 항목 | 내용 |
|---|---|
| **포트 ↔ 0.33.13 API 차이** | hola-mini 가 이식한 QMK 는 0.33.13 보다 한참 이전이다. `keycodes.h` 재편 · `keyboard.c` 스캔 흐름 · `eeconfig` 레이아웃 등에서 차이가 날 수 있다. **착수하면 컴파일부터 해 보고**, 차이가 크면 QMK 리비전을 hola-mini 쪽에 맞춰 내릴지 포트를 올릴지 정한다 |
| sparse 범위 | `quantum/` 만으로 헤더가 부족하면 `sparse-checkout set` 에 `platforms/`, `tmk_core/` 를 추가한다. `keyboards/` 만 빠지면 크기는 여전히 작다 |
| 매트릭스 크기 | 16×16 = 256키. consumer / system 키는 usage page 가 달라 별도 처리가 필요하다 |
| 여러 키보드 동시 연결 | 허브를 켜면 가능해진다. 비트맵을 OR 로 합칠지 결정 |
| Fn 을 레이어 키로 못 쓴다 | HHKB 의 Fn 은 리포트에 안 나온다. keymap 설계 시 잘 안 쓰는 키(F7·F8 등)를 레이어 키로 지정하는 우회를 쓴다 → [04-usb-device-hid.md](04-usb-device-hid.md#알아-둘-것--변환기의-근본적인-제약) |
| keyboard 페이지 볼륨키 | HHKB 는 `0x80`/`0x81`(keyboard 페이지)로 보낸다. consumer 미디어키로 바꿔 줄지 |
