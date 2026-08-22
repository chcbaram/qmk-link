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
| **다음** | **05 QMK** — `fetch_upstream.py` + `qmk/via/port/` + `link/` (HID report → 가상 매트릭스) |

04 단계 실측: FLASH 82,668 B / 2 MB (3.94%), RAM 37,028 B / 512 KB (7.06%).
`0483:5305 QMK-LINK` 로 열거된다 — HID(keyboard / extra / raw) + CDC 복합 장치.
`clk_sys` 는 CLI 에서 120,000,000 Hz 확인.

**지금 동작하는 것**: HHKB Lite 2 를 USB-A 에 꽂고 타이핑하면 PC 에 그대로 입력된다.
QMK 는 아직 없다 — `ap.c` 의 `updateKeyboard()` 가 리포트를 그대로 넘기는 패스스루다.

**미검증**: 마우스 패스스루(USB 마우스 없음), BIOS 화면, Windows / Linux.

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
| `~/hdd/git/wish-he/firmware/wish-he` | **USB descriptor 인터페이스 배치**. `src/hw/driver/usb/cherryusb/usb_desc.h` 주석에 IF0 함정과 인터페이스 순서 근거가 적혀 있다 |
| `~/hdd/git/convex/firmware/convex-qmk` | **마우스 · consumer · system 을 한 인터페이스에 리포트 ID 로 담는 구성**. `src/hw/driver/usb/usb_hid/usbd_hid.c` |
| `~/hdd/git/baram-kbd-tester` | USB **호스트** 쪽 관례. `src/hw/driver/usbh/` 배치 |

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
python3 ../firm-sdk/tools/flash.py build/src/qmk-link.uf2   # download
python3 ../firm-sdk/tools/flash.py --list                   # 볼륨/포트 확인

arm-none-eabi-size -A build/src/qmk-link.elf                          # 크기
../firm-sdk/.picotool/picotool/picotool info -a build/src/qmk-link.uf2  # uf2 확인
```

VSCode 는 `firmware/qmk-link/prj/qmk-link.code-workspace` 를 연다.
`build-build` 가 기본 빌드 태스크다.

---

## 확정된 결정과 이유

| 결정 | 이유 |
|---|---|
| **clk_sys = 120MHz** | Pico-PIO-USB 는 120MHz 의 배수를 요구한다. RP2350 기본값 150MHz 로는 동작하지 않는다. 나중에 바꾸면 그 사이 초기화된 PIO/USB 클럭이 어긋나므로 1단계부터 `bspInit()` 맨 앞에서 확정한다 |
| **USB-A 는 R13 제거가 전제** | R13(1.5K, D+ 풀업)이 실장되어 있었고 제거했다. 두면 빈 포트가 "FS 장치 연결됨"으로 보이고 LS 키보드 판별이 깨진다. 같은 측정으로 `GPIO12 = D+`, `GPIO13 = D−` 를 확증했다 → [hardware.md](hardware.md#r10--r13--usb-a-포트의-풀업-해결됨) |
| **`common/` 은 손대지 않는다** | 다른 baram 프로젝트와 공유하는 자산이다. 바꿔야 하면 **먼저 확인받는다.** 현재 `src/common/` 은 전부 원본과 바이트 단위로 동일하다 (`cli.*` · `led.h` · `log.*` · `uart.h` · `ws2812.h` · `qbuffer.*` 는 hola-mini, `swtimer.h` · `reset.h` 는 nano-ch32h417). 헤더는 공유본을 쓰되 **구현은 이 보드에 필요한 것만** 한다 |
| **백그라운드 처리는 `delay()` 에 태운다** | `bsp.c` 의 `delay()` 가 `cliLoopIdle()` 을 돌린다 (NU87-TinyDK 관례). 그래야 기존 코드를 안 고치고도 USB 가 계속 돈다. 한때 `cliDelay()` 를 새로 만들고 `cliKeepLoop()` 을 고쳤다가, 이 관례를 따르는 쪽으로 되돌렸다 |
| **CRLF 는 지원하지 않는다** | `CLI_KEY_ENTER` 는 CR 만 본다. 호스트 도구가 CR 만 보내면 된다. LF 무시를 넣어 봤지만 정작 `cliKeepLoop()` 중단은 못 고쳐서 되돌렸다 |
| **리셋 더블클릭은 SDK 라이브러리로** | RP2350 은 RUN 리셋 때 RAM 전원이 내려간다(실측). SRAM · watchdog scratch · POWMAN scratch 전부 지워진다. RP2350 전용 `POWMAN_CHIP_RESET.DOUBLE_TAP` 비트를 `pico_bootsel_via_double_reset` 이 쓴다 |
| **VID/PID = `0483:5305`** | VID 는 baram 키보드 공통(0x0483). PID 는 안 겹치는 값 — 5200 hs-k / 5201 45k-hs / 5207 qmk-8k / 5211 convex / 5220 Lucky65 / 5230 hola-mini / 5300 esp32-qmk / 5301 qmk-h7s / **5304 wish-he** 다음 |
| **변환기는 키보드가 보내는 것만 받는다** | HHKB 의 Fn 은 리포트에 안 나온다. 조합의 결과 키코드만 온다. Fn 을 레이어 키로 쓸 수 없다 → [04-usb-device-hid.md](04-usb-device-hid.md#알아-둘-것--변환기의-근본적인-제약) |
| **부트 키보드는 IF0 이어야 한다** | 일부 BIOS 가 IF0 만 본다 (wish-he 의 `usb_desc.h` 에 기록된 함정). 04단계에서 CDC 를 뒤로 밀고 키보드를 IF0 으로 옮긴다 |
| **HID 리포트 ID 는 QMK 값** | mouse 2 / system 3 / consumer 4 / NKRO 6. `qmk/port/protocol/report.h` 와 어긋나면 05단계에서 곤란해진다 |
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
| **EEPROM 쓰기 vs core1** | 6단계 | flash 를 쓰는 동안 XIP 가 멈추는데 core1 이 PIO USB 를 돌고 있다. `flash_safe_execute()` + `multicore_lockout_victim_init()` 로 core1 을 잠가야 한다. **06단계 착수 시 이것부터 실험한다** |
| **hola-mini 포트 ↔ QMK 0.33.13 API 차이** | 5단계 | hola-mini 가 이식한 QMK 는 0.33.13 보다 한참 이전이다. `keycodes.h` 재편 · `keyboard.c` 스캔 흐름 · `eeconfig` 레이아웃 등에서 차이가 날 수 있다. 착수 시 **먼저 컴파일 가능 여부부터 확인**하고, 차이가 크면 QMK 리비전을 내릴지 포트를 올릴지 정한다 |
| sparse-checkout 범위 | 5단계 | `quantum/` 만으로 부족하면 `sparse-checkout set` 에 경로를 추가한다. `keyboards/` 만 빠지면 크기는 여전히 작다 |
| Windows / Linux 에서 `flash.py` | 해당 OS 실기가 있을 때 | macOS 에서만 검증했다. `setup-windows.md` · `setup-linux.md` 도 미검증이다 |

---

## 문서 읽는 순서

1. **`00-context.md`** (이 문서)
2. [`roadmap.md`](roadmap.md) — 전체 단계와 현재 상태
3. [`hardware.md`](hardware.md) — 핀 배정. 코드 만지기 전에 반드시
4. 현재 단계 문서 (`01-led.md` … `08-finalize.md`)
5. [`usb-stack.md`](usb-stack.md) — 2~4단계 작업이면 필수
