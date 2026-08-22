# 01-led — 빌드하고 굽는 경로를 뚫는다

**상태: ✅ 완료**

## 목표

WS2812(L1) 를 500ms 주기로 점멸시킨다. 그게 다다.

진짜 목적은 LED 가 아니라 **그 뒤에 깔리는 것들**이다.

- 프로젝트 골격 (`main → bsp → hw → ap`)
- `firm-sdk` 에 pico-sdk 2.3.0 고정
- 커스텀 보드 헤더 (flash 2MB 를 제대로 잡는다)
- 120MHz 시스템 클럭
- OS 무관 다운로드 도구 (`flash.py`)

## 배경 / 근거

첫 단계에서 빌드와 다운로드 경로가 뚫려 있지 않으면 이후 단계를 디버깅할 수단이 없다.
이 보드는 **디버그 UART 핀조차 없어서** 02단계(CDC) 전까지 관측 수단이 LED 하나뿐이다.
그래서 LED 를 가장 먼저 살린다.

**120MHz 클럭도 여기서 확정한다.** 3단계(PIO USB host)에서 필요한 값이지만,
나중에 바꾸면 그 사이 초기화된 PIO / USB 클럭이 어긋난다.
→ [usb-stack.md](usb-stack.md#클럭--120mhz)

## 설계

### 계층

```
main.c        hwInit() → apInit() → apMain()
  └ hw.c      bspInit() → ws2812Init() → ledInit()
      └ bsp.c set_sys_clock_khz(120000)
```

`ws2812Init()` 이 `ledInit()` 보다 **반드시 먼저**다. `led.c` 가 ws2812 위에 올라가기 때문이다.

### LED 계층 — ws2812 위의 래퍼

이 보드에는 단순 GPIO LED 가 없다. WS2812B-0807 하나(L1, GPIO16)가 전부다.
그래서 `led.c` 를 ws2812 드라이버 위에 얹어 `ledOn/ledOff/ledToggle(_DEF_LED1)`
관용구를 그대로 유지한다. 이후 QMK RGB 도 같은 ws2812 레이어를 재사용한다.

`ws_pixel_t` 가 `{b, r, g, w}` 순이라 색은 `WS2812_COLOR()` 가 아니라
**`WS2812_RGB(r,g,b)`** 로 만든다.

### WS2812 전송 순서 — 실기에서 잡은 것

표준 WS2812B 는 G,R,B 순인데 **이 보드의 부품은 R,G,B 다.**
처음 구웠을 때 초록을 보냈는데 빨강이 켜져서 발견했다.

hola-mini 판은 순서를 유니온 레이아웃에 박아 두고 `data << 8` 을 그대로 밀어냈다.
여기서는 순서를 `hw_def.h` 의 `HW_WS2812_ORDER_RGB` 로 빼고
`ws2812Refresh()` 가 논리 색상 → 전송 순서로 변환한다. → [hardware.md](hardware.md)

### 보드 헤더

`pico2` 를 그대로 쓰면 flash 가 4MB 로 잘못 잡힌다 (실제는 W25Q16JV = 2MB).
`src/bsp/board/qmk_link.h` 를 만들고 `PICO_BOARD=qmk_link` 로 지정한다.
보드 헤더 위치는 `rp2040_fw` 관례(`src/bsp/board/`)를 따랐다.

`PICO_BOARD_HEADER_DIRS` 는 `pico_sdk_init()` 안의 `generic_board.cmake` 가 읽으므로
**반드시 `pico_sdk_init()` 이전에** 설정해야 한다.

`PICO_DEFAULT_UART*` / `PICO_DEFAULT_LED_PIN` 은 정의하지 않는다 — 회로에 없다.

### 로그 / CLI 없음

`pico_stdio_usb` / `pico_stdio_uart` 를 둘 다 끈다.
`pico_stdio_usb` 는 자체 device descriptor 를 갖고 있어 나중에 HID 와 공존이 안 된다.
CDC 는 02단계에서 직접 만든다. → [usb-stack.md](usb-stack.md#pico_stdio_usb-를-쓰지-않는-이유)

## 구현 항목

### firm-sdk

- [x] `pico-sdk` 서브모듈 @ 2.3.0
- [x] 무선용 서브모듈 deinit (btstack / mbedtls / cyw43-driver / lwip) → 338MB → **49MB**
- [x] `firm-sdk/README.md`
- [x] `firm-sdk/tools/flash.py`

### qmk-link

- [x] `CMakeLists.txt` — SDK 경로 · 보드 헤더 · `KEY_PROTOCOL` 옵션 자리
- [x] `src/CMakeLists.txt` — glob · include · `pico_generate_pio_header`
- [x] `src/bsp/board/qmk_link.h` — 보드 헤더
- [x] `src/bsp/bsp.c` / `bsp.h` — 120MHz, `delay/millis/micros`
- [x] `src/hw/hw_def.h` — `_USE_HW_LED`, `_USE_HW_WS2812`
- [x] `src/hw/hw.c` / `hw.h` — `_USE_HW_*` 가드
- [x] `src/hw/driver/ws2812.c` + `pio/ws2812.pio` — 전송 순서를 `hw_def.h` 로 분리
- [x] `src/hw/driver/led.c` — ws2812 래퍼 (신규 작성)
- [x] `src/ap/ap.c` — 500ms 점멸
- [x] `src/main.c` / `main.h`, `src/common/def.h`, `common/hw/include/{led,ws2812}.h`
- [x] `.vscode/{tasks,settings,c_cpp_properties}.json`, `prj/qmk-link.code-workspace`

hola-mini 에서 가져온 파일: `main.c/h`, `def.h`, `led.h`, `ws2812.h`, `ws2812.pio`, `ap.h`, `ap_def.h`
(출처: `~/hdd/git/baram-hola-mini/firmware/hola-mini/src/`)

수정해서 가져온 것:
- `bsp.c` — `set_sys_clock_khz(120000)` 추가, 안 쓰는 include 제거
- `hw_def.h` — `QMK_KEYMAP_CONFIG_H` 제거, LED/WS2812 만
- `ws2812.c` — CLI 블록 제거, 하드코딩 `gpio=3`/`pio0`/`sm 0` → `HW_WS2812_*`,
  전송 순서를 `HW_WS2812_ORDER_RGB` 로 분리
- `led.c` — GPIO 직접 제어 → ws2812 래퍼로 새로 작성

## 완료 판정

### 빌드 (검증됨)

```
Target board (PICO_BOARD) is 'qmk_link'.
Using board configuration from .../src/bsp/board/qmk_link.h
Defaulting platform (PICO_PLATFORM) to 'rp2350-arm-s' based on PICO_BOARD setting.
TinyUSB available at .../lib/tinyusb/hw/bsp/rp2040; enabling build support for USB.

Memory region         Used Size  Region Size  %age Used
           FLASH:        8472 B         2 MB      0.40%
             RAM:        4416 B       512 KB      0.84%
```

**`FLASH` 가 2 MB 로 나오는 게 핵심**이다. 4MB 면 보드 헤더가 안 잡힌 것이다.

### uf2 (검증됨)

```
$ picotool info -a build/src/qmk-link.uf2
File build/src/qmk-link.uf2 family ID 'rp2350-arm-s':
 name:                qmk-link
 target chip:         RP2350
 image type:          ARM Secure
 sdk version:         2.3.0
 pico_board:          qmk_link
 boot2_name:          boot2_w25q080
```

### 실기 (검증됨)

1. Key1(BOOTSEL) 을 누른 채 Type-C 연결 → `RP2350` 볼륨
2. `python3 ../firm-sdk/tools/flash.py build/src/qmk-link.uf2`
3. **WS2812(L1) 가 500ms 주기로 어두운 초록 점멸** ✅

점멸 주기가 눈에 띄게 어긋나면 클럭 설정을 의심한다
(`set_sys_clock_khz` 가 실패하면 `bspInit()` 이 `false` 를 돌려준다).

### clean 빌드 재현 (검증됨)

`rm -rf build` 후 configure + build 통과.

## 열린 질문

| 항목 | 이월 |
|---|---|
| 클럭이 실제로 120MHz 인지 직접 확인 | 02단계 — CLI 에서 `clock_get_hz(clk_sys)` 를 찍는다. 01단계에서는 관측 수단이 없어 점멸 주기로 갈음했다 |
| 다른 WS2812 채널 | 지금은 1개뿐이라 순서 검증이 R·G 두 채널로 끝났다. B 는 소거법이다 |
| macOS 외 OS 에서 `flash.py` 경로 탐지 | 실기가 있는 환경에서 확인. [setup-windows.md](setup-windows.md) · [setup-linux.md](setup-linux.md) 는 미검증 |
| `bspInit()` 실패 시 처리 | 지금은 `false` 만 돌려주고 그냥 진행한다. 02단계에서 로그가 생기면 경고를 찍는다 |
