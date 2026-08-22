# usb-stack — USB 스택 설계

이 프로젝트의 가장 큰 구조적 결정이다. 02~04단계 문서가 이 문서를 참조한다.

---

## 이중 역할 (dual role)

TinyUSB 는 device 스택과 host 스택을 동시에 돌릴 수 있다.
pico-sdk 2.3.0 이 TinyUSB 와 `lib/tinyusb/src/portable/raspberrypi/pio_usb/hcd_pio_usb.c` 를
이미 포함하고 있다(확인함). TinyUSB 의 `examples/dual/host_hid_to_device_cdc` 가 정확히 우리 구성이다.

```
RHPort 0 = RP2350 네이티브 USB   → OPT_MODE_DEVICE  (Type-C, PC)
RHPort 1 = PIO USB (GPIO12/13)  → OPT_MODE_HOST    (USB-A, 키보드)
```

`tusb_config.h` 핵심:

```c
#define CFG_TUD_ENABLED           1
#define CFG_TUH_ENABLED           1
#define CFG_TUSB_RHPORT0_MODE     OPT_MODE_DEVICE
#define CFG_TUSB_RHPORT1_MODE     (OPT_MODE_HOST | OPT_MODE_FULL_SPEED)
#define CFG_TUH_RPI_PIO_USB       1
#define BOARD_TUH_RHPORT          1

#define CFG_TUH_HID               4   // 키보드가 여러 인터페이스를 낼 수 있다
#define CFG_TUH_HUB               1   // 허브 경유 대응 (08단계)
#define CFG_TUH_DEVICE_MAX        (CFG_TUH_HUB ? 5 : 1)

#define CFG_TUD_HID               3   // keyboard / extra / raw(VIA·Vial)
#define CFG_TUD_CDC               1
#define CFG_TUD_VENDOR            1   // picotool reset 인터페이스
```

`Pico-PIO-USB` 자체는 pico-sdk 에 없다. `firm-sdk/Pico-PIO-USB` 서브모듈로 따로 붙이고
SDK 의 `hcd_pio_usb.c` 와 함께 빌드에 넣는다.
(SDK 는 `tinyusb_pico_pio_usb` 타깃을 만들어 주지 않는다 — 확인함.)

---

## 클럭 — 120MHz

**Pico-PIO-USB 는 `clk_sys` 가 120MHz 의 배수여야 한다. RP2350 기본값 150MHz 로는 동작하지 않는다.**

`bspInit()` 맨 앞에서 `set_sys_clock_khz(120000, ...)` 를 호출하고, 그 뒤에 PIO / USB 를 초기화한다.
**1단계부터 넣어 두었다.** 나중에 바꾸면 그 사이 초기화된 주변장치의 클럭이 어긋난다.

WS2812 PIO 의 clkdiv 는 `clock_get_hz(clk_sys)` 로 계산되므로 클럭이 바뀌어도 자동 대응한다.

---

## 코어 분배

Pico-PIO-USB 는 타이밍에 민감해서 전용 코어를 권장한다 (공식 예제도 그렇다).

```
core0 : tud_task() + QMK 처리 + CLI + LED
core1 : tuh_task()   (PIO USB host 전용)
```

두 코어 사이 키 이벤트 전달은 `common/core/qbuffer.c` 링버퍼를 쓴다.
hola-mini 도 `multicore_launch_core1()` 로 같은 패턴이다.

---

## PC 쪽 복합 장치 구성

```
IAD ─ CDC (comm + data)       : CLI · 로그
HID Keyboard                   : boot protocol 호환 (BIOS 에서도 동작)
HID Extra                      : consumer / system control, NKRO
HID Raw (usage page 0xFF60)    : VIA / Vial 통신 (IN + OUT)
Vendor (RESET)                 : picotool 호환용 (부수적)
```

Windows 에서 CDC + HID 복합 장치를 제대로 잡으려면 **IAD** 가 필요하다.
VID/PID 는 개발 중 임시값을 쓰되 `info.json` / `vial.json` 과 반드시 일치시킨다 (08단계).

---

## `pico_stdio_usb` 를 쓰지 않는 이유

`pico_stdio_usb` 는 **자체 device descriptor** 를 들고 있어서 우리 HID descriptor 와 공존할 수 없다.

- `pico_stdio_usb` 라이브러리를 링크하지 않는다
- descriptor 는 `src/hw/driver/usb/usbd_desc.c` 에서 직접 작성한다
- `logPrintf` / `cliPrintf` 는 `cdc.h` 인터페이스를 통해 CDC 로 나간다
- vendor RESET 인터페이스는 pico-sdk 의 `src/rp2_common/pico_stdio_usb/reset_interface.c` 와
  `stdio_usb_descriptors.c` 에서 RESET 인터페이스 부분
  (`bInterfaceClass=0xFF, SubClass=0x00, Protocol=0x01`) 만 떼어 이식한다

---

## 펌웨어 업데이트

RP2350 은 BOOTROM 의 UF2 부트로더가 항상 살아 있으므로 **자체 부트로더를 만들지 않는다.**
굽는 경로는 **BOOTSEL 대용량 저장장치에 uf2 복사** 하나로 통일하고,
"실행 중인 펌웨어를 BOOTSEL 로 넣는 방법" 만 여러 개 둔다.

| 리부트 트리거 | 구현 | 단계 |
|---|---|---|
| Key1(BOOTSEL) 누른 채 연결 | 하드웨어 | 1 |
| **CDC 1200bps touch** | `tud_cdc_line_coding_cb()` 에서 baud==1200 이고 DTR 이 내려가면 `reset_usb_boot(0,0)` | 2 |
| vendor RESET 인터페이스 | pico-sdk `reset_interface.c` 이식 (picotool 호환용) | 2 |
| CLI `reset boot` | 같은 `reset_usb_boot(0,0)` | 2 |
| VIA/Vial "bootloader" | QMK `bootloader_jump()` 포팅 → 같은 함수 | 6 |

**1200bps touch 를 1순위로 둔다.** Arduino 이래의 표준 관례이고,
파이썬 쪽은 `pyserial` 로 포트를 1200bps 로 열었다 닫기만 하면 되므로 OS 별 드라이버 이슈가 없다.
vendor RESET 인터페이스는 libusb 를 타서 Windows 에서 WinUSB 드라이버가 필요하다 — 그래서 부수적이다.

`reset_usb_boot()` 포팅은 hola-mini 의
`src/ap/modules/qmk/port/platforms/bootloader.c` 를 그대로 쓸 수 있다.
플래시 2MB 안에 QMK + TinyUSB 가 들어가므로 A/B 슬롯은 필요 없다.

### PICOBOOT 을 파이썬으로 구현하지 않는 이유

picotool 이 쓰는 USB 벌크 프로토콜이라 볼륨 마운트 없이 구울 수 있어 매력적이지만,
`pyusb` + libusb 의존이 생기고 Windows 에서 드라이버 설치가 필요해진다.
UF2 파일 복사는 OS 가 이미 대용량 저장장치로 잡아 주므로 의존성이 0 이다.

---

## 3단계에서 결정할 사항

- 허브를 거친 키보드 지원 여부 (`CFG_TUH_HUB`)
- USB-A 키보드를 boot protocol / report protocol 중 무엇으로 돌릴지
  (report protocol 이 NKRO 를 살리지만 리포트 디스크립터 파싱이 필요하다)
- 부팅 시 host 포트 VBUS 공급 타이밍 (J1 VBUS 는 VSYS 직결)
- **R10 / R13 실장 여부** — [hardware.md](hardware.md) 참고. 실장되어 있으면 호스트 열거가 실패한다
