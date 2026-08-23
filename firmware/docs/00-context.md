# 00-context — 이어서 작업하기 위한 문서

**다른 세션 / 다른 사람이 이 파일 하나만 읽고 바로 이어서 작업할 수 있게 하는 문서다.**
저장소 코드를 아무리 읽어도 알 수 없는 것들을 여기 모은다.
단계를 끝낼 때마다 "현재 위치" 와 `roadmap.md` 의 상태를 갱신한다.

---

## 이 프로젝트가 뭔가

`qmk-link` 는 **RP2350-USB-A 보드로 만드는 USB 키보드 변환기**다.

```
[일반 USB 키보드] --USB-A(J1)--> [RP2350] --Type-C--> [PC]
                    PIO USB host            네이티브 USB device
                                            HID(kbd/extra/raw) + CDC + vendor
```

USB-A 에 꽂힌 아무 키보드나 받아서 QMK 의 키 처리(레이어 / 매크로 / 탭홀드 등)를 태운 뒤,
PC 에는 **VIA / Vial 로 편집 가능한 키보드**로 보이게 한다.
덤으로 Type-C 에 CDC 를 얹어 CLI · 디버그 · 펌웨어 업데이트를 처리한다.

---

## 현재 위치

| | |
|---|---|
| **완료** | **01 LED** — 프로젝트 골격, firm-sdk, 빌드/다운로드 경로, 120MHz 클럭 |
| **완료** | **02 CLI/CDC** — USB CDC + CLI/log/swtimer, 1200bps touch, 더블클릭 리셋 |
| **완료** | **03 USB HOST** — Pico-PIO-USB, core1 전용 태스크. HHKB Lite 2 열거·리포트 수신 확인 |
| **완료** | **04 HID DEVICE** — HID kbd/extra/raw + CDC 복합. 패스스루로 PC 타이핑 확인 |
| **완료** | **05 QMK** — QMK 0.33.13 이식. `link/` 가상 매트릭스, QMK 거쳐 PC 입력 확인 |
| **완료** | **06 VIA** — flash EEPROM, raw HID, dynamic keymap, 커스텀 메뉴. 전부 실기 확인 |
| **완료** | **07 VIAL** — vial 트리. 장치가 정의를 내주는 것까지 확인 |
| **완료** | **08 마감** — 미디어키 · 키보드 여러 대 · suspend 소등 · VID/PID · README |
| **완료** | **09-1 학습 마법사** — `web/`, GitHub Actions 로 Pages 배포 |
| **완료** | **09-2 온디바이스 저장** — 저장소 · 업로드 도구 · **Vial 정의 서빙** |
| **완료** | **09-2-4 PID 전환** — VIA 도 정의를 자동으로 고른다 (`0x5400` + SLOT) |
| **완료** | **09-3 키맵 프로파일** — **SLOT 이 곧 프로파일.** 키보드마다 키맵이 따로다 |

**릴리스** : **[v1.1.0](https://github.com/chcbaram/qmk-link/releases/tag/v1.1.0)** — 09단계 전부.
[v1.0.0](https://github.com/chcbaram/qmk-link/releases/tag/v1.0.0) 은 08단계까지.
**웹 마법사** : <https://chcbaram.github.io/qmk-link/>

실측: via FLASH 122,764 B / vial 141,164 B, RAM 238~258 KB / 512 KB (copy_to_ram).
RAM 이 커진 것은 EEPROM 섀도가 16KB → 80KB 가 됐기 때문이다 (키맵 프로파일 17벌).
`0483:5305 QMK-LINK` 로 열거된다 — HID(keyboard / extra / raw) + CDC 복합 장치.
★ 09단계부터 **PID 가 고정이 아니다.** 꽂힌 키보드의 레이아웃 SLOT 에 따라
`0x5400`+SLOT 으로 바뀐다. 담아 둔 것이 없을 때만 `0x5305` 다.
`clk_sys` 는 CLI 에서 120,000,000 Hz 확인.

**지금 동작하는 것**

- HHKB Lite 2 를 USB-A 에 꽂고 타이핑하면 QMK 를 거쳐 PC 에 입력된다
- **QMK 는 부팅 때 자동으로 올라온다** (`apInit()`). `qmk start` 는 이제 재기동용이다
- VIA 프로토콜 응답 — version 13, layer count 8, 키맵 읽기/쓰기
- 키맵이 **내장 플래시에 저장되어 재부팅을 넘는다** (via `0x1A0000` / vial `0x1B4000`, 각 80KB)
- VIA 커스텀 메뉴 6개 — 탭텀 / hold-okp / permissive / retro / NKRO / 패스스루
- VIA 의 bootloader 버튼 → BOOTSEL
- VIA 의 Key Tester > Test Matrix — 누른 키의 usage 가 배열에서 반짝인다
- **미디어키** — Consumer 인터페이스를 파싱해 그대로 흘린다 (QMK 를 거치지 않는다)
- **키보드 여러 대** — 인스턴스별 비트맵을 OR 로 합친다
- **레이아웃 저장소** — `0x1D0000` 부터 8KB x 16칸. `tools/kbd_upload.py` 로 담고 꺼낸다.
  **via · vial 공용이다** (EEPROM 과 반대다). 펌웨어를 바꿔 구워도 남는다
- **PID 전환** — 꽂힌 키보드의 SLOT 에 따라 `0x5400`+SLOT 으로 재열거한다. VIA 가 정의를 스스로 고른다
- **키맵 프로파일** — SLOT 이 곧 프로파일이다. 키보드를 바꿔 꽂으면 **키맵도 같이 바뀐다**.
  새 SLOT 은 쓰던 키맵을 물려받는다
- **★ Vial 정의를 장치가 내준다** — 꽂힌 키보드에 맞는 칸을 찾아 그 정의를 준다.
  저장된 것이 없으면 컴파일에 박힌 풀사이즈 기본값이 나간다
- **Vial 트리** — `-DKEY_PROTOCOL=vial`. 장치가 자기 정의를 내준다 (552 B, LZMA)
  기능 전부 켜 뒀다 — QMK Settings 15항목 · tap dance 16 · combo 16 ·
  key override 8 · alt repeat 8 · 매크로 16(버퍼 11.7KB)

**진단은 CLI `key info`** 로 한다. 이 단계의 고장들은 `mounted` / `drop` 으로는
안 보였다 → [06-via.md](06-via.md#진단-도구--key-info)

**미검증**: 미디어키(컨슈머 인터페이스 있는 키보드 없음), 키보드 두 대 동시,
suspend 소등, 마우스 패스스루, BIOS 화면, Windows / Linux
→ [08-finalize.md](08-finalize.md#미검증--장비환경이-없어서-못-잰-것)

BOOTSEL 진입 경로 (전부 실기 확인):
1. `flash.py` 의 CDC 1200bps touch — 버튼 없이 굽는다
2. Key2(Reset) 빠르게 두 번 — `pico_bootsel_via_double_reset`
3. Key1(BOOTSEL) + Key2(Reset) — 하드웨어. 펌웨어가 뻗어도 된다
4. CLI `reset boot`

---

## 참조 프로젝트 경로

**이 저장소 안에 없는 정보다. 기록해 두지 않으면 다음 세션이 못 찾는다.**

| 경로 | 무엇을 참고하나 |
|---|---|
| `~/hdd/git/baram-hola-mini/firmware/hola-mini` | **RP2350 + QMK 포팅 원본.** `src/` 계층 구조(main→bsp→hw→ap), ws2812/led/cli/log/uart 드라이버, `src/ap/modules/qmk/port/` (platforms · protocol · matrix · bootloader) 전체가 여기서 온다 |
| `~/hdd/git/nano-ch32h417/firmware` | `firm-sdk` / `firm-prj` 폴더 관례, CMakeLists 골격, `.vscode/tasks.json` 스타일 |
| `~/hdd/git/NU87-TinyDK/firmware` | `firm-sdk/README.md` 톤, `tools/*.py` 파이썬 도구 관례(`flash.py`, `list_ports.py`, `sync_sdk.py`) |
| `~/hdd/git/rp2040_fw` | **보드 헤더를 `src/bsp/board/` 에 두는 관례** |
| `~/hdd/git/wish-he/firmware/wish-he` | **USB descriptor 인터페이스 배치** (`usb_desc.h` 주석에 IF0 함정)과 **QMK 출력 드라이버 연결** (`src/ap/modules/qmk/port/driver_usb.c` — `host_driver_t`, `keyboard_protocol_get()` 함정, 전송 정책). 05단계 참고 구현 |
| `~/hdd/git/convex/firmware/convex-qmk` | **마우스 · consumer · system 을 한 인터페이스에 리포트 ID 로 담는 구성**. `src/hw/driver/usb/usb_hid/usbd_hid.c` |
| `~/hdd/git/baram-kbd-tester` | USB **호스트** 쪽 관례. `src/hw/driver/usbh/` 배치 |
| `~/hdd/git/vial-qmk` | 프리셋 원본. `web/tools/gen_presets.py` 가 여기서 좌표+키맵을 뽑는다. baram 키보드도 `keyboards/baram/` 에 들어가 있다 (F1-40 = `geon/f1_40/staggered`) |
| `~/hdd/git/baram-qmk-8k/src/ap/modules/qmk/keyboards/baram/45k/json` | **BARAM 45K 프리셋의 출처.** `keyboard.json` 은 폭(w)이 빠진 매트릭스 표라 못 쓴다 — VIA 정의(`*.JSON`)를 쓴다 |

---

## 빌드 환경

| | |
|---|---|
| 컴파일러 | `arm-none-eabi-gcc` 14.2.1 (Arm GNU Toolchain) |
| 빌드 | cmake 4.4.2 / ninja 1.13.2 / make |
| SDK | **pico-sdk 2.3.0**, `firmware/firm-sdk/pico-sdk` 서브모듈 |
| 파이썬 | python3 (+ `pyserial`, 1200bps touch 에만 쓰이는 선택 의존) |
| picotool | **설치 불필요.** SDK 가 `firm-sdk/.picotool/` 에 받아 빌드한다 |

`~/dev/pico/pico-sdk` (2.1.1) 가 로컬에 있지만 **쓰지 않는다.**
`PICO_SDK_PATH` 환경변수도 쓰지 않는다 — CMakeLists 가 서브모듈 경로를 직접 지정한다.

### 클론 직후

```bash
git submodule update --init                                          # pico-sdk
git -C firmware/firm-sdk/pico-sdk submodule update --init lib/tinyusb
```

`--recursive` 를 쓰면 안 된다. 무선용 서브모듈(btstack 220MB 등)까지 받아 338MB 가 된다.
위처럼 받으면 49MB.

---

## 자주 쓰는 명령

`firmware/qmk-link/` 에서 실행한다.

```bash
cmake -S . -B build                                     # configure
cmake --build build -j16                                # build
python3 ../firm-sdk/tools/flash.py build/src/qmk-link-via.uf2   # download
python3 ../firm-sdk/tools/flash.py --list                   # 볼륨/포트 확인

arm-none-eabi-size -A build/src/qmk-link.elf                          # 크기
../firm-sdk/.picotool/picotool/picotool info -a build/src/qmk-link.uf2  # uf2 확인
```

VSCode 는 `firmware/qmk-link/prj/qmk-link.code-workspace` 를 연다.
`build-build` 가 기본 빌드 태스크다.

### QMK 원본 (05단계부터)

```bash
python3 firmware/firm-sdk/tools/fetch_upstream.py           # 받는다 (없으면 CMake 가 막는다)
python3 firmware/firm-sdk/tools/fetch_upstream.py --check   # 리비전 확인
python3 firmware/firm-sdk/tools/fetch_upstream.py --update  # 최신으로 upstream.json 갱신
```

### CLI 로 상태 보기 — 이번 이식에서 실제로 쓴 방법

CDC 콘솔에 붙는 파이썬 조각. 터미널을 띄우는 것보다 자동화가 쉽다.
**엔터는 반드시 `\r` 만 보낸다** (`\r\n` 은 keep-loop 명령을 즉시 중단시킨다).

```python
import serial, time, glob
s = serial.Serial(sorted(glob.glob("/dev/cu.usbmodem14*"))[0], 115200, timeout=0.5)
time.sleep(0.4); s.reset_input_buffer()
s.write(b"qmk info\r"); time.sleep(1.5)
print(s.read(32768).decode(errors="replace").replace("\r\n","\n"))
```

주요 명령:

| 명령 | 용도 |
|---|---|
| `usbh info` | 호스트 상태 · 열거된 키보드 · `rx / drop` |
| `usbh dump` | 올라온 HID 리포트를 그대로 찍는다 |
| `usb info` | device 상태 · `hid ready` · `host led` |
| `qmk start` | **QMK 기동. 부팅 때 자동으로 안 켜진다** |
| `qmk info` | QMK 상태 · link 비트맵 · QMK 매트릭스 |
| `log boot` | 부팅 로그 재생 (CDC 열거 전 로그가 여기 쌓인다) |
| `reset boot` | BOOTSEL 로 재부팅 |

---

## 소스 구조 (06단계 기준)

```
firmware/qmk-link/src/
├── main.c                      hwInit -> apInit -> apMain
├── bsp/
│   ├── bsp.c                   ★ delay() 안에서 cliLoopIdle() 을 돌린다
│   └── board/qmk_link.h        핀 상수는 전부 여기
├── common/                     ★ 손대지 않는다 (원본과 바이트 단위 동일)
├── hw/
│   ├── hw_def.h                _USE_HW_* 기능 스위치
│   ├── driver/usb/             device : descriptor · HID · CDC
│   │   ├── usbd_desc.c         ★ 인터페이스 배치 + 리포트 디스크립터 (sizeof 때문에 한 파일)
│   │   └── usbd_hid.c          tud_hid_* 콜백, 송신 헬퍼, usb_device_state 연결
│   └── driver/usbh/            host : core1 태스크 + HID 큐
└── ap/
    ├── ap.c                    ★ updateKeyboard() / cliLoopIdle() — 여기엔 진입점만 둔다
    └── modules/
        ├── display/            사람에게 보여주는 것 — led_status.c (LED 상태 표시)
        ├── link/               HID usage -> 16x16 비트맵 · 저장소 · 우리 raw HID 명령
        └── qmk/
            ├── qmk.c           QMK 기동 · 패스스루 · CLI (via·vial 공용)
            └── via/
                ├── CMakeLists.txt   ★ 어떤 upstream 파일을 넣을지 여기서 정한다
                ├── version.h
                └── port/            driver_usb · matrix · via_port · platforms
```

키보드 정의는 소스 밖에 따로 있다 (**wish-he 관례**):

```
firmware/qmk-link/
├── keyboards/qmk-link/         via · vial 두 트리가 같은 것을 본다
│   ├── config.h                QMK 설정 (CMake 가 QMK_KEYMAP_CONFIG_H 로 넘긴다)
│   ├── keymap.c                usage 패스스루 키맵
│   ├── layout-kle.json         ★ 손으로 편집하는 건 이것 하나. 범례는 '키 이름'
│   ├── menus.json              VIA 커스텀 메뉴 (손으로 씀)
│   └── layout-via.json         생성물 — VIA 앱 Design 탭에 넣는 정의
└── tools/gen_keymap.py         KLE + menus -> layout-via.json
```

```bash
python3 tools/gen_keymap.py          # 생성
python3 tools/gen_keymap.py --show   # 매핑 표 + KLE 에 빠진 usage
python3 tools/gen_keymap.py --check  # 생성물이 최신인지만 확인
```

**`ap/` 에는 진입점만 둔다.** 자족적인 것은 `ap/modules/<이름>/` 로 뺀다.

`ap/modules/` 중 **`qmk` 와 `link` 만** 프로젝트 glob 에서 제외된다
(`src/CMakeLists.txt` 의 `EXCLUDE REGEX "/ap/modules/(qmk|link)/"`).
그 둘은 QMK 모듈의 CMakeLists 가 정한다 — 안 그러면 중복 컴파일되고
`-include quantum/led.h` 도 못 받는다.

★ **QMK 와 무관한 모듈은 거기 얹지 않는다.** 얹으면 트리(via·vial)마다 목록을
두 벌 관리하게 되고, 필요도 없는 컴파일 옵션이 따라붙는다. 그냥 glob 이 긁는다.

## ★ 계속 돌아야 하는 것은 `cliLoopIdle()` 에 넣는다

`bsp.c` 의 `delay()` 와 `cliKeepLoop()` 이 이 함수를 돌린다 (NU87-TinyDK 관례).
오래 도는 CLI 명령(`qmk matrix`, `usbh dump` …) 안에서는 `apMain()` 이 진행되지
않으므로, 여기에 없는 것은 그 동안 멈춘다.

```c
void cliLoopIdle(void)
{
  usbUpdate();          // tud_task
  usbdHidUpdate();      // 못 보낸 키 리포트 재시도
  updateKeyboard();     // USB 호스트 큐 -> link
  qmkUpdate();          // eeprom_task + raw HID + keyboard_task
}
```

**`apMain()` 은 이 함수를 부른다. 목록을 두 벌로 두지 않는다.**

```c
void apMain(void)
{
  while(1)
  {
    cliLoopIdle();
    ledStatusUpdate();
    cliMain();
  }
}
```

### 이 규칙으로 세 번 데였다

| 증상 | 빠져 있던 것 |
|---|---|
| `rx/drop 689/657` — 리포트의 95% 유실 | `updateKeyboard()` |
| `qmk matrix` 가 아무것도 못 보여줌 | `qmkUpdate()` |
| **키를 떼도 PC 가 계속 눌린 것으로 앎** | `usbdHidUpdate()` — `cliLoopIdle()` 에만 넣고 `apMain()` 에는 안 넣었다 |

마지막 것이 목록을 한 군데로 합친 이유다. 두 벌이면 한쪽에만 넣는 실수가 반드시 난다.
증상은 이렇게 보였다 — `보류 1` 인데 `나중에보냄 0`, 즉 못 보낸 리포트가 갇힌 채
재시도가 한 번도 안 돌았다. `key info` 로 본다.

## 확정된 결정과 이유

| 결정 | 이유 |
|---|---|
| **clk_sys = 120MHz** | Pico-PIO-USB 는 120MHz 의 배수를 요구한다. RP2350 기본값 150MHz 로는 동작하지 않는다. 나중에 바꾸면 그 사이 초기화된 PIO/USB 클럭이 어긋나므로 1단계부터 `bspInit()` 맨 앞에서 확정한다 |
| **USB-A 는 R13 제거가 전제** | R13(1.5K, D+ 풀업)이 실장되어 있었고 제거했다. 두면 빈 포트가 "FS 장치 연결됨"으로 보이고 LS 키보드 판별이 깨진다. 같은 측정으로 `GPIO12 = D+`, `GPIO13 = D−` 를 확증했다 → [hardware.md](hardware.md#r10--r13--usb-a-포트의-풀업-해결됨) |
| **`common/` 은 손대지 않는다** | 다른 baram 프로젝트와 공유하는 자산이다. 바꿔야 하면 **먼저 확인받는다.** 현재 `src/common/` 은 전부 원본과 바이트 단위로 동일하다 (`cli.*` · `led.h` · `log.*` · `uart.h` · `ws2812.h` · `qbuffer.*` 는 hola-mini, `swtimer.h` · `reset.h` 는 nano-ch32h417). 헤더는 공유본을 쓰되 **구현은 이 보드에 필요한 것만** 한다 |
| **백그라운드 처리는 `delay()` 에 태운다** | `bsp.c` 의 `delay()` 가 `cliLoopIdle()` 을 돌린다 (NU87-TinyDK 관례). 그래야 기존 코드를 안 고치고도 USB 가 계속 돈다. 한때 `cliDelay()` 를 새로 만들고 `cliKeepLoop()` 을 고쳤다가, 이 관례를 따르는 쪽으로 되돌렸다 |
| **CRLF 는 지원하지 않는다** | `CLI_KEY_ENTER` 는 CR 만 본다. 호스트 도구가 CR 만 보내면 된다. LF 무시를 넣어 봤지만 정작 `cliKeepLoop()` 중단은 못 고쳐서 되돌렸다 |
| **리셋 더블클릭은 SDK 라이브러리로** | RP2350 은 RUN 리셋 때 RAM 전원이 내려간다(실측). SRAM · watchdog scratch · POWMAN scratch 전부 지워진다. RP2350 전용 `POWMAN_CHIP_RESET.DOUBLE_TAP` 비트를 `pico_bootsel_via_double_reset` 이 쓴다 |
| **SLOT 이 곧 키맵 프로파일** | 개념을 둘로 늘리지 않는다. `[SLOT 추가]` 하면 배열과 키맵이 같이 생기고, 새 SLOT 은 쓰던 키맵을 물려받는다. `kbd_sel_t` 의 `profile` 바이트는 남겨 뒀지만 안 쓴다 |
| **EEPROM 을 저장소 *아래*로** | 프로파일 17벌이면 트리당 80KB 다. 저장소 위(`0x1F0000`)에서 키우면 선택 표와 부딪히고, 저장소를 옮기면 담아 둔 배열이 날아간다. 아래(`0x1A0000`/`0x1B4000`)로 내리면 저장소·선택 표 주소가 그대로다 |
| **키맵 프로파일은 `eeprom.c` 에서 주소만 옮긴다** | `nvm_dynamic_keymap.c` 를 복사해 오는 대신 `eeprom_read_block`/`write_block` 에서 옮긴다. upstream 을 안 건드리고 트리당 파일 한 벌만 본다 |
| **VID/PID = `0483:5305`** | VID 는 baram 키보드 공통(0x0483). PID 는 안 겹치는 값 — 5200 hs-k / 5201 45k-hs / 5207 qmk-8k / 5211 convex / 5220 Lucky65 / 5230 hola-mini / 5300 esp32-qmk / 5301 qmk-h7s / **5304 wish-he** 다음 |
| **변환기는 키보드가 보내는 것만 받는다** | HHKB 의 Fn 은 리포트에 안 나온다. 조합의 결과 키코드만 온다. Fn 을 레이어 키로 쓸 수 없다 → [04-usb-device-hid.md](04-usb-device-hid.md#알아-둘-것--변환기의-근본적인-제약) |
| **부트 키보드는 IF0 이어야 한다** | 일부 BIOS 가 IF0 만 본다 (wish-he 의 `usb_desc.h` 에 기록된 함정). 04단계에서 CDC 를 뒤로 밀고 키보드를 IF0 으로 옮긴다 |
| **HID 리포트 ID 는 QMK 값** | mouse 2 / system 3 / consumer 4 / NKRO 6. `qmk/port/protocol/report.h` 와 어긋나면 05단계에서 곤란해진다 |
| **QMK 는 upstream 을 받아 쓴다** | `firm-sdk/upstream.json` 이 리비전 고정(`0.33.13`), `fetch_upstream.py` 가 sparse 로 받는다(9.6MB). **`tmk_core/protocol` 도 원본을 쓴다** — vendoring 하지 않는다 |
| **QMK 는 부팅 때 자동으로 올린다** (06단계~) | 05단계까지는 `qmk start` 로만 켰다 — 이식 중에 죽으면 USB 가 통째로 안 올라와서다. VIA 까지 확인된 지금은 `apInit()` 에서 올린다. 되살릴 길은 남아 있다 (Key2 더블클릭 → BOOTSEL). 기동에 실패하면 04단계 패스스루로 떨어진다 |
| **★ 펌웨어를 통째로 RAM 에서 돌린다** (`copy_to_ram`) | 플래시 소거·기록 중 XIP 가 멈추는데 core1 이 PIO USB 를 돈다. lockout 으로 세우면 SOF 가 끊겨 키보드가 서스펜드에 빠지고 **리셋 전까지 안 낫는다.** RAM 에서 돌면 정지 자체가 없다 — 소거 중 core1 `tuh_task` 3회 → **37,456회**. `PICO_FLASH_ASSUME_CORE1_SAFE=1` 로 lockout 을 끈다. RAM 152KB/512KB → [06-via.md](06-via.md) |
| **`mounted` / `drop` 으로는 USB 호스트 고장을 못 본다** | TinyUSB 의 `hidh_xfer_cb` 가 전송 실패를 무시하고 길이 0 으로 콜백한다. 겉보기엔 리포트가 계속 오는데 내용이 없다. **CLI `key info`** 가 이걸 보라고 있다 (drain / link / 버림) |
| **뒤처리로 덮으려 하지 않는다** | `pio_usb_host_stop()` 은 0.7.2 에서 죽은 코드고(무한 대기), `hcd_event_device_remove/attach` 재열거는 떼기만 되고 다시 못 붙는다. 둘 다 실제로 겪었다 |
| **EEPROM 은 flash + RAM 섀도 + 지연 플러시** | 소거·기록 중 XIP 가 멈추는데 core1 이 PIO USB 를 돈다. `flash_safe_execute()` 로 core1 을 세운다. **실측 소거 30ms + 기록 10ms = 41ms 동안 USB 호스트가 통째로 멈춘다.** 6회 연속에도 키보드가 살아 있어 진입을 확정했다. 지연 플러시가 핵심 — VIA 는 키 하나에 바이트 쓰기가 수백 번 온다 → [06-via.md](06-via.md#1-eeprom-을-flash-로--이-단계의-핵심-리스크였다) |
| **VIA 와 Vial EEPROM 을 떼어 놓는다** | `0x1F0000` / `0x1F4000`, 각 16KB. 같은 보드에 두 펌웨어를 번갈아 구울 수 있는데 영역을 공유하면 상대가 남긴 바이트를 자기 레이아웃으로 읽는다. 매직이 우연히 맞으면 초기화도 안 되고 엉뚱한 키맵이 나온다 |
| **`flash.h` 는 wish-he 인터페이스 그대로** | 주소는 **플래시 오프셋**이다. rp2040 계열 `flash.c` 는 반대로 XIP 절대주소를 받으므로 그쪽 코드를 베껴 올 때 주의. 영역 가드는 rp2040_fw 의 `flash_tbl` 을 참고하되 "겹치면 통과" 가 아니라 "완전히 들어가야 통과" 로 좁혔다 |
| **트리는 `config.h` 와 EEPROM 영역만 가른다** | vial-qmk 의 QMK 베이스가 0.33.13 과 사실상 같아서(`host_driver_t`·`usb_device_state`·`quantum/nvm` 모두 동일) `port/` 929줄이 거의 그대로 옮겨갔다. ★ `port/platforms/eeprom.c` 의 `EE_FLASH_BEGIN` 을 안 바꾸면 두 펌웨어가 조용히 섞인다 → [07-vial.md](07-vial.md) |
| **Vial 의 `BUILD_ID` 는 고정한다** | vial-qmk 는 이걸 빌드마다 난수로 만든다. 그러면 다시 구울 때마다 EEPROM 이 무효가 되어 키맵이 초기화된다. `0x00514C4B`("QLK") 로 박고 EEPROM 배치를 바꿀 때만 올린다 |
| **탭홀드 주인이 트리마다 다르다** | via 는 우리 커스텀 메뉴(`*_PER_KEY`), vial 은 Vial 자신. `*_PER_KEY` 를 vial 에 두면 `vial.c` 의 `get_tapping_term()` 과 겹치고, upstream 의 `#endif` 중첩 실수까지 밟는다 |
| **키보드 정의는 `keyboards/qmk-link/`** | wish-he 관례. **`layout-kle.json` 하나만 손으로 편집**하고 `tools/gen_keymap.py` 가 `layout-via.json` 을 만든다. KLE 범례는 주소가 아니라 **키 이름**이다 — 주소를 손으로 적으면 반드시 어긋난다 |
| **VIA 배열은 풀사이즈 + 서랍** | 매트릭스 좌표가 HID usage 라 **그림은 물리 PCB 가 아니다.** TKL/65%/60% 는 풀사이즈의 부분집합이라 그대로 덮이고, ISO Enter 도 ANSI Enter 도 `0x28` 이라 **레이아웃 옵션이 필요 없다.** ANSI 에 없는 usage(F13~F24 / ISO·JIS / 편집·미디어)는 아래에 서랍으로 붙인다. **그림에 없는 키도 동작한다** — 못 고칠 뿐이다 |
| **VIA 커스텀 메뉴는 메뉴 > 그룹 > 컨트롤 3단** | `menus[i].content[]` 에 컨트롤을 바로 넣으면 VIA 가 정의를 통째로 거부한다(`must NOT have additional properties`). 앱에 넣어 봐야 아는 실수라 `gen_keymap.py` 가 검사한다 |
| **빌드 옵션을 VIA 커스텀 메뉴로 뺀다** | 꽂는 키보드가 매번 달라서 탭텀·탭홀드를 다시 구워 바꾸는 게 특히 불편하다. Vial 의 QMK settings 와 같은 효과를 얻으면서 **웹앱을 포크하지 않는다.** ★ `*_PER_KEY` 매크로가 없으면 QMK 가 `get_*()` 를 아예 부르지 않는다 |
| **`led.h` 이름 충돌** | `common/hw/include/led.h`(baram)와 `quantum/led.h`(QMK)가 같은 이름이다. include 경로 순서로는 못 푼다. QMK 소스에만 `-include quantum/led.h` 를 강제한다 (`qmk/via/CMakeLists.txt`) |
| **허브 지원은 필수다** | `CFG_TUH_HUB=1`. HHKB Lite 2 처럼 허브 내장 키보드가 흔하다. 끄면 연결은 감지되는데 열거가 끝나지 않는다 |
| **PIO USB 가 pio0 를 통째로 쓴다** | SM 0·1·2 + DMA ch0. 그래서 WS2812 는 pio1 이다 |
| **PIO USB 는 core1 전용** | 타이밍 때문이다. 알람풀도 core1 에서 만들어야 SOF 가 core1 에서 돈다. 코어 간 리포트 전달은 `pico/util/queue.h` |
| **WS2812 전송 순서 = R,G,B** | 표준 WS2812B 는 G,R,B 지만 이 보드 부품은 R,G,B 다. 실기에서 확인했다(초록을 보냈는데 빨강이 켜짐). `hw_def.h` 의 `HW_WS2812_ORDER_RGB` 로 분리했다 |
| **커스텀 보드 헤더** (`src/bsp/board/qmk_link.h`) | `pico2` 를 쓰면 flash 가 4MB 로 잘못 잡힌다. 실제는 W25Q16JV = 2MB |
| **`pico_stdio_usb` 미사용** | 자체 device descriptor 를 갖고 있어 우리 HID descriptor 와 공존이 안 된다. CDC 는 2단계에서 직접 만든다 |
| **자체 부트로더 없음** | RP2350 은 BOOTROM UF2 가 항상 살아 있다. 굽는 경로는 BOOTSEL 볼륨에 uf2 복사 하나로 통일하고, "실행 중 펌웨어를 BOOTSEL 로 넣는 방법"만 여러 개 둔다 |
| **파이썬 다운로더** (`flash.py`) | OS 별 picotool 바이너리를 관리하지 않는다. uf2 복사는 OS 가 대용량 저장장치로 잡아 주므로 의존성이 0 이다 |
| **QMK / Vial 독립 트리** | vial-qmk 는 QMK 의 포크다. `dynamic_keymap.c` · `keycodes.h` · flash 저장 구조 · QMK 베이스 버전까지 갈린다. 한 트리에서 `#ifdef` 로 버티면 업스트림 갱신마다 깨진다 |
| **업스트림을 저장소에 넣지 않음** | 서브모듈이면 클론마다 790MB. `--depth 1 --filter=blob:none --sparse` 로 `quantum/` 만 받으면 리포당 5MB 이고 `upstream/` 은 gitignore 라 저장소 증가는 0 |

---

## 하지 않기로 한 것

- **매트릭스를 줄여 Vial 매트릭스 테스터 살리기** — 12x15 로 줄이면 되는 것은
  확인했다(실물이 보내는 usage 는 173개뿐이라 180칸에 들어간다). 하지만
  **좌표 = usage 성질**을 잃는다. 이 프로젝트의 모든 문서와 도구가 그 위에 서
  있고, 저장된 키맵도 전부 깨진다. 대체 수단이 있다 — CLI `qmk matrix`, 그리고
  09단계의 학습 마법사 → [07-vial.md](07-vial.md)

다시 제안되지 않게 이유와 함께 적는다.

| | 왜 안 하나 |
|---|---|
| 자체 부트로더 / A/B 슬롯 | BOOTROM UF2 가 이미 있다. 2MB flash 에 슬롯을 나눌 이유가 없다 |
| UART 로그 | **이 보드엔 디버그 UART 핀 자체가 없다.** GPIO0/1 도 헤더로 안 나온다. 로그는 USB CDC 로만 |
| `pico_stdio_usb` | descriptor 충돌 (위 표) |
| OS 별 picotool 바이너리 동봉 | `flash.py` 로 대체 |
| PICOBOOT 을 파이썬으로 구현 | 볼륨 마운트 없이 구울 수 있어 매력적이지만 `pyusb`+libusb 의존이 생기고 Windows 에서 WinUSB 드라이버가 필요해진다 |
| QMK/Vial 소스를 저장소에 복사(벤더링) | 업스트림 갱신이 수동 diff 가 되고 GPL2 코드가 우리 히스토리에 섞인다 |
| QMK/Vial 서브모듈 | 790MB (위 표) |

---

## 업스트림 리비전

5단계부터 `firmware/firm-sdk/upstream.json` 이 단일 진실 원천이다.

| | 리포지토리 | 현재 값 |
|---|---|---|
| QMK | `github.com/qmk/qmk_firmware` | 릴리스 태그 **`0.33.13`** |
| Vial | `github.com/vial-kb/vial-qmk` | 태그 없음, **`vial` 브랜치** (`dd43959a`, 2026-07-26) |

갱신: `python3 firm-sdk/tools/fetch_upstream.py --update`

---

## 열린 질문 / 미확인

| 항목 | 언제 결정 | 내용 |
|---|---|---|
| boot vs report protocol | 3단계 | report protocol 이 NKRO 를 살리지만 리포트 디스크립터 파싱이 필요하다 |
| ~~VID / PID~~ ✅ | — | `info.json` / `vial.json` 과 반드시 일치해야 한다. 04단계에서 descriptor 가 바뀌므로 PID 를 한 번 올린다 |
| ~~EEPROM 이 RAM / 쓰기 vs core1~~ ✅ | — | 06단계에서 해결. `flash_safe_execute()` + 지연 플러시. 실측 41ms 정지, 견딜 만하다 |
| ~~hola-mini 포트 ↔ QMK 0.33.13 API 차이~~ ✅ | — | hola-mini 가 이식한 QMK 는 0.33.13 보다 한참 이전이다. `keycodes.h` 재편 · `keyboard.c` 스캔 흐름 · `eeconfig` 레이아웃 등에서 차이가 날 수 있다. 착수 시 **먼저 컴파일 가능 여부부터 확인**하고, 차이가 크면 QMK 리비전을 내릴지 포트를 올릴지 정한다 |
| ~~sparse-checkout 범위~~ ✅ | — | `quantum/` 만으로 부족하면 `sparse-checkout set` 에 경로를 추가한다. `keyboards/` 만 빠지면 크기는 여전히 작다 |
| **VIA 웹앱 실물** | 07단계 전 | 프로토콜은 hidapi 로 전부 확인했지만 `layout-via.json` 을 앱 Design 탭에 넣어 그림이 제대로 나오는지는 아직 미확인 (PID 전환까지 얹혀 있으니 같이 본다) |
| ~~키보드마다 다른 레이아웃/키맵~~ ✅ | 09단계 | 끝났다. **SLOT 이 곧 프로파일** — 배열도 키맵도 SLOT 단위로 갈린다 → [09-keyboard-profile.md](09-keyboard-profile.md) |
| Windows / Linux 에서 `flash.py` | 해당 OS 실기가 있을 때 | macOS 에서만 검증했다. `setup-windows.md` · `setup-linux.md` 도 미검증이다 |

---

## 다음에 할 것

**09단계까지 전부 끝났고 v1.1.0 으로 배포했다.** 급한 것은 없다.
아래는 "필요해지면" 목록이다 — 필요해지기 전에 만들지 않는다.

### 지금 보드에 올라가 있는 것

**via 펌웨어** · SLOT 0 = HHKB Lite 2 (적용 중) · SLOT 1 = 같은 키보드의 변형본.

```bash
cd firmware/qmk-link
python3 tools/kbd_upload.py list          # SLOT · 적용 중 · PID
```

CLI 로 더 보려면 → 아래 "CLI 로 상태 보기".

### 되돌릴지 정해야 할 것

`875c76f` **[실험] 줄을 클릭하면 편집** — `[편집]` 버튼을 없애고 SLOT 줄 전체를
클릭 대상으로 바꿨다. 써 보고 아니다 싶으면 `git revert 875c76f` 하나로 돌아간다.
(적용은 라디오, 편집은 줄 — 위험한 쪽을 작은 과녁으로 둔 것이 요지다)

### 필요해지면

| | |
|---|---|
| VIA 커스텀 메뉴에 **프로파일 수동 선택** | 지금은 SLOT 이 프로파일을 정한다. 사람이 따로 고르고 싶어지면 `kbd_sel_t` 의 `profile` 바이트를 쓴다 (비워 뒀다) |
| **product string 해시로 키보드 식별** | 읽는 것은 이미 된다(`0x09 HOST_INFO`). 표시에만 쓴다. vid/pid 가 겹치는 싸구려 키보드를 만나면 `kbdStoreSelect(vid,pid,hash)` 에 해시를 넣는다 |
| **매크로 · 탭댄스도 프로파일마다** | 지금은 키맵만 갈린다. 나머지는 공유다. 같은 방식(`kmRemap` 구간 추가)으로 늘릴 수 있다 |
| SLOT 이름을 **키보드 이름으로 자동** | 마법사가 이미 채워 주지만 손으로 고칠 수 있다 |

### 못 하는 것 (재검토 불필요)

- **Vial 매트릭스 테스터** — `(cols/8+1)*rows <= 28` 을 16x16(48)이 넘어 vial-qmk 가
  그 코드를 빼고 빌드한다. 잠금 해제와 무관하고 배열을 줄여도 안 된다
  (좌표가 곧 usage 라 16x16 이어야 한다). 마법사의 "지금 눌린 키" 로 대신한다
- **원본 키보드의 Fn** — 조합의 결과 키코드만 USB 로 온다

### 실기로 못 잰 것 (장비 없음)

미디어키(Consumer 인터페이스가 있는 키보드 없음) · 키보드 두 대 동시 ·
suspend 소등 · 마우스 패스스루 · BIOS 화면 · Windows/Linux 의 `flash.py`.

---

## ★ 09단계에서 데인 것들 — 전부 "조용히 틀리는" 종류다

| 어긴 것 | 어떻게 드러났나 |
|---|---|
| **컨트롤 전송을 열거 중에 던졌다** | `tuh_hid_mount_cb()` 안에서 product string 을 요청했더니 컨트롤 슬롯이 차 있어 `false` 만 돌아왔다. 오류가 안 나니 이름이 그냥 비어 보였다 → core1 루프에서 재시도한다 |
| **EEPROM 주소를 upstream 이 정하게 두면** | 매크로 영역이 "EEPROM 끝까지" 로 잡혀서 뒤에 둔 키맵 프로파일을 통째로 덮어쓴다. `DYNAMIC_KEYMAP_EEPROM_MAX_ADDR` 을 같이 못 박아야 한다 |
| **`eeprom.c` 가 config.h 를 안 읽고 있었다** | `#ifndef TOTAL_EEPROM_BYTE_COUNT` 기본값(16384)이 config 값과 우연히 같아서 아무도 몰랐다. 그 값을 바꾸는 순간 조용히 어긋난다 |
| **남의 VIA 정의는 범례가 좌표다** | 우리 정의는 좌표가 곧 usage 라 `"3,6"` 을 그대로 믿어도 됐다. 그런데 BARAM 45K 의 VIA json 도 `"3,6"` 인데 그건 **그 키보드의 매트릭스**다 (스페이스). 그대로 믿으면 usage 0x36(마침표)로 읽어 엉뚱한 키를 다 배운 상태가 된다 → `matrix` 가 16x16 일 때만 믿는다 |


| | |
|---|---|
| **upstream 훅에 기대지 말 것** | `via_command_kb()` 는 qmk 전용, `raw_hid_receive_kb()` 는 vial 전용이다. 반대편에서는 링커가 통째로 버려서 **빌드 크기가 한 바이트도 안 변한다.** 우리가 raw HID 큐를 직접 비우는 자리(`qmkUpdate`)에서 걷어낸다 — `link_cmd` · `vial_port` 둘 다 그 방식이다 |
| **같은 raw HID 를 넷이 나눠 쓴다** | VIA · Vial · 웹 마법사 · `kbd_upload.py`. 다른 쪽이 물어본 답이 우리에게 배달된다. VIA 가 `Received invalid protocol version` 을 낸 것이 그것이었다(`160` = `0xA0`). **앞 두 바이트를 대조**해야 한다 |
| **VID 만 보고 장치를 열지 말 것** | `0x0483` 은 baram 키보드들이 같이 쓴다. wish-he 도 usage page `0xFF60` 을 갖고 있어서 도구가 **그쪽을 열고** 엉뚱한 답을 받았다. PID 까지 본다 |
| **좌표와 키맵은 같은 매크로에서** | 프리셋 범례가 한 칸씩 밀렸다. 지금은 **토큰 수 == 자리 수** 검사로 막는다 (`web/tools/gen_presets.py`) |

---

## 도구 한눈에

```bash
cd firmware/qmk-link

# 빌드
cmake -S . -B build            && cmake --build build -j16          # VIA
cmake -S . -B build-vial -DKEY_PROTOCOL=vial && cmake --build build-vial -j16

python3 ../firm-sdk/tools/flash.py build/src/qmk-link-via.uf2
python3 ../firm-sdk/tools/flash.py build-vial/src/qmk-link-vial.uf2

# 배열 정의 생성 (layout-kle.json 하나가 원본)
python3 tools/gen_keymap.py            # -> layout-via / layout-vial / 정의 헤더
python3 tools/gen_keymap.py --show     # 매핑 표 + 빠진 usage

# 보드에 레이아웃 담기
python3 tools/kbd_upload.py list
python3 tools/kbd_upload.py put 0 keyboards/qmk-link/layout-vial.json --name "HHKB Lite 2"
python3 tools/kbd_upload.py get 0 back.json
python3 tools/kbd_upload.py erase 0
```

**웹 마법사로 다 된다** — 파이썬 도구는 파일을 그대로 담고 꺼낼 때 쓴다.
`http://localhost:8000` 또는 <https://chcbaram.github.io/qmk-link/>
① 보드 연결 → ② 담긴 것(트리) → ③ 배열+마법사 → ④ 보드에 저장

```bash
cd web
python3 -m http.server 8000            # WebHID 는 file:// 로 안 된다
python3 tools/gen_presets.py > presets.js   # vial-qmk 경로가 필요하다
```

**웹 배포는 자동이다** — `web/` 을 고쳐 `main` 에 push 하면
`.github/workflows/pages.yml` 이 올린다. 별도 브랜치 없다.

### CLI (CDC 115200, 엔터는 `\r` 만)

| | |
|---|---|
| `key info` | 키 입력 경로 진단. **`mounted`/`drop` 으로는 안 보이는 고장을 여기서 본다** |
| `qmk info` · `qmk matrix` | QMK 상태 · 눌린 usage |
| `qmk eeprom` | 키맵 저장 상태 |
| `kbd info` | 레이아웃 저장소 · 지금 적용 SLOT · 가려진 SLOT |
| `kbd sel n\|auto` | 쓸 SLOT 을 고정한다 (프로파일도 같이 바뀐다) |
| `kbd erase n` | SLOT 지우기 |
| `usbh info` · `flash info` | USB 호스트 · 플래시 |

---

## 문서 읽는 순서

1. **`00-context.md`** (이 문서)
2. [`roadmap.md`](roadmap.md) — 전체 단계와 현재 상태
3. [`hardware.md`](hardware.md) — 핀 배정. 코드 만지기 전에 반드시
4. 현재 단계 문서 (`01-led.md` … `08-finalize.md`)
5. [`usb-stack.md`](usb-stack.md) — 2~4단계 작업이면 필수
