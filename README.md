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
| [02 CLI/CDC](firmware/docs/02-cli-cdc.md) | ⬜ |
| [03 USB HOST](firmware/docs/03-usb-host.md) | ⬜ |
| [04 HID DEVICE](firmware/docs/04-usb-device-hid.md) | ⬜ |
| [05 QMK](firmware/docs/05-qmk.md) | ⬜ |
| [06 VIA](firmware/docs/06-via.md) | ⬜ |
| [07 VIAL](firmware/docs/07-vial.md) | ⬜ |
| [08 마감](firmware/docs/08-finalize.md) | ⬜ |

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

cd firmware/qmk-link
cmake -S . -B build
cmake --build build -j16
```

> `--recursive` 를 쓰지 않는다. 무선용 서브모듈(btstack 220MB 등)까지 받아 338MB 가 된다.
> 위처럼 받으면 49MB.

## 다운로드

```bash
python3 ../firm-sdk/tools/flash.py build/src/qmk-link.uf2
```

OS 별 picotool 바이너리가 필요 없다. `flash.py` 하나로 macOS / Windows / Linux 를 처리한다.

- 실행 중인 펌웨어가 있으면 **1200bps touch** 로 BOOTSEL 재부팅시킨다 (02단계부터)
- 안 되면 **Key1(BOOTSEL)** 을 누른 채 USB 를 연결한다
- `pyserial` 은 touch 에만 쓰이는 선택 의존이다

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
