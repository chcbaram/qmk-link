# qmk-link

**USB 키보드를 QMK / VIA / Vial 키보드로 바꿔주는 어댑터.**

```
[일반 USB 키보드] --USB-A(J1)--> [RP2350] --Type-C--> [PC]
                    PIO USB host            HID(kbd/extra/raw) + CDC
```

USB-A 에 꽂힌 아무 키보드나 받아서 QMK 의 키 처리(레이어 · 매크로 · 탭홀드 등)를 태운 뒤,
PC 에는 **VIA / Vial 로 편집 가능한 키보드**로 보이게 한다.
Type-C 쪽에 CDC 도 얹어 CLI · 디버그 · 펌웨어 업데이트를 처리한다.

---

## 보드 — RP2350-USB-A

| 항목 | 값 |
|---|---|
| MCU | RP2350A (QFN60) |
| Flash | W25Q16JVUXIQ — 2MB |
| XOSC | 12MHz |
| 시스템 클럭 | 120MHz (Pico-PIO-USB 요구사항) |
| WS2812 | GPIO16 (L1) |
| USB-A (J1) | D+ = GPIO12, D− = GPIO13 |
| Type-C | 네이티브 USB |
| Key1 / Key2 | BOOTSEL / Reset |

회로도: [`hardware/RP2350-USB-A.pdf`](hardware/RP2350-USB-A.pdf) ·
핀 배정 근거: [`firmware/docs/hardware.md`](firmware/docs/hardware.md)

---

## 진행 상태

| 단계 | 상태 |
|---|---|
| [01 LED](firmware/docs/01-led.md) | ✅ 완료 |
| [02 CLI/CDC](firmware/docs/02-cli-cdc.md) | ✅ 완료 |
| [03 USB HOST](firmware/docs/03-usb-host.md) | ✅ 완료 |
| [04 HID DEVICE](firmware/docs/04-usb-device-hid.md) | ✅ 완료 |
| [05 QMK](firmware/docs/05-qmk.md) | ✅ 완료 |
| [06 VIA](firmware/docs/06-via.md) | ✅ 완료 |
| [07 VIAL](firmware/docs/07-vial.md) | ✅ 완료 |
| [08 마감](firmware/docs/08-finalize.md) | ✅ 완료 |
| [09 키보드 프로파일](firmware/docs/09-keyboard-profile.md) | ⬜ 검토 완료 |

전체 계획: [`firmware/docs/roadmap.md`](firmware/docs/roadmap.md)

---

## 빌드

개발환경 구축은 OS 별 문서를 본다 —
[macOS](firmware/docs/setup-macos.md) ·
[Windows](firmware/docs/setup-windows.md) ·
[Linux](firmware/docs/setup-linux.md)

```bash
git clone https://github.com/chcbaram/qmk-link.git
cd qmk-link

git submodule update --init                                          # pico-sdk 2.3.0
git -C firmware/firm-sdk/pico-sdk submodule update --init lib/tinyusb

python3 firmware/firm-sdk/tools/fetch_upstream.py                    # QMK / vial-qmk
```

> `--recursive` 를 쓰지 않는다. 무선용 서브모듈(btstack 220MB 등)까지 받아 338MB 가 된다.
> 위처럼 받으면 49MB.

> QMK / vial-qmk 는 **저장소에 넣지 않는다.** `firm-sdk/upstream.json` 이 리비전을
> 고정하고 `fetch_upstream.py` 가 sparse 로 받는다 (둘 합쳐 25MB, gitignore).

**VIA 판**

```bash
cd firmware/qmk-link
cmake -S . -B build
cmake --build build -j16
python3 ../firm-sdk/tools/flash.py build/src/qmk-link-via.uf2
```

**Vial 판**

```bash
cmake -S . -B build-vial -DKEY_PROTOCOL=vial
cmake --build build-vial -j16
python3 ../firm-sdk/tools/flash.py build-vial/src/qmk-link-vial.uf2
```

USB 제품 이름이 `QMK-LINK VIA` / `QMK-LINK VIAL` 로 갈리므로
지금 어느 펌웨어가 올라가 있는지 OS 에서 바로 보인다.

### 다운로드에 대해

OS 별 picotool 바이너리가 필요 없다. `flash.py` 하나로 macOS / Windows / Linux 를 처리한다.

- 실행 중인 펌웨어가 있으면 **1200bps touch** 로 BOOTSEL 재부팅시킨다
- 안 되면 **Key2(Reset) 를 빠르게 두 번** — 펌웨어가 뻗어도 된다 (하드웨어 기능)
- 그것도 안 되면 **Key1(BOOTSEL)** 을 누른 채 USB 를 연결한다
- `pyserial` 은 touch 에만 쓰이는 선택 의존이다

---

## 쓰는 법

1. USB-A(J1) 에 아무 USB 키보드나 꽂는다
2. Type-C 를 PC 에 연결한다 — LED 가 **초록**이면 둘 다 붙은 것이다
3. 그대로 타이핑된다. 설정 없이 동작한다

### 키맵 편집

| | |
|---|---|
| **VIA** | [usevia.app](https://usevia.app) → Design 탭에 [`keyboards/qmk-link/layout-via.json`](firmware/qmk-link/keyboards/qmk-link/layout-via.json) 을 넣는다 |
| **Vial** | [vial.rocks](https://vial.rocks) — **정의를 장치가 직접 내주므로 파일이 필요 없다** |

Vial 은 편집 전에 **잠금 해제**가 필요하다 — **좌우 Shift 를 함께 5초 이상** 누른다.

### 배열에 대해

배열은 **풀사이즈로 그려져 있다.** 이 보드의 매트릭스 좌표가 HID usage 라
(`row = usage>>4`, `col = usage&0xF`) TKL · 75% · 65% · 60% 는 전부 그 부분집합이다.
ISO Enter 든 ANSI Enter 든 같은 `0x28` 이라 레이아웃 옵션도 필요 없다.

**그림에 없는 키도 동작한다** — 못 고칠 뿐이다.
어떤 usage 가 오는지는 CLI `qmk matrix` 로 본다.

### LED

| 상태 | 색 |
|---|---|
| PC 가 잠듦 | 소등 |
| PC 미연결 | 빨강 빠른 점멸 |
| 키보드 없음 | 주황 느린 점멸 |
| 정상 | 초록 |
| CapsLock | 보라 |
| 키 눌림 | 흰색 짧게 |

### CLI

Type-C 의 CDC 포트로 붙는다 (115200). **엔터는 `\r` 만** — `\r\n` 은 반복 명령을 중단시킨다.

| 명령 | |
|---|---|
| `qmk info` / `qmk matrix` | QMK 상태 · 눌린 usage 보기 |
| `qmk eeprom` | 키맵 저장 상태 |
| `key info` | 키 입력 경로 진단 (리포트가 어디서 막히는지) |
| `usbh info` | USB 호스트 · 꽂힌 키보드 |
| `flash info` / `flash test` | 플래시 |
| `reset boot` | BOOTSEL 진입 |

---

## 구조

```
firmware/
├── docs/           설계 · 로드맵 · 개발환경 문서
├── firm-sdk/       외부 SDK (pico-sdk 2.3.0) + 파이썬 도구
└── qmk-link/       펌웨어 프로젝트
    └── src/        main → bsp → hw → ap
```

이어서 작업하려면 [`firmware/docs/00-context.md`](firmware/docs/00-context.md) 부터 읽는다.

## License

[MIT](LICENSE)
