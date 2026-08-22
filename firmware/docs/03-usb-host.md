# 03-usb-host — USB-A 에 꽂힌 키보드를 읽는다

**상태: ✅ 완료**

## 목표

USB-A(J1) 에 일반 USB 키보드를 꽂으면 CLI 에 HID report 가 찍힌다.

## 배경 / 근거

이 프로젝트에서 **가장 위험한 단계**다. PIO USB 는 타이밍에 민감하고
하드웨어 쪽 미확인 항목도 여기서 드러난다. QMK 를 얹기 전에 먼저 검증한다.

01단계에서 물려받는 것: **120MHz 클럭**, R13 제거.
02단계에서 물려받는 것: **CDC 로그** — 이게 없으면 이 단계는 디버깅이 불가능하다.

## 설계

→ [usb-stack.md](usb-stack.md) 가 전체 근거다.

### 빌드 배선

`Pico-PIO-USB` 는 `firm-sdk/Pico-PIO-USB` 서브모듈(0.7.2, 260KB).

**`pico_sdk_init()` 이전에 `PICO_PIO_USB_PATH` 만 지정하면 나머지는 SDK 가 다 한다.**
tinyusb 의 `hw/bsp/rp2040/family.cmake` 가 `check_and_add_pico_pio_usb_support()` 를
최상위에서 부르는데, 그게 `tinyusb_pico_pio_usb` 타깃을 만들고
`hcd_pio_usb.c` / `dcd_pio_usb.c` 를 tinyusb 에 붙이고 PIO 헤더까지 생성한다.

```cmake
set(PICO_PIO_USB_PATH ${CMAKE_CURRENT_LIST_DIR}/../firm-sdk/Pico-PIO-USB)
...
target_link_libraries(${PRJ_NAME} tinyusb_host tinyusb_pico_pio_usb)
```

### PIO 자원 — WS2812 를 pio1 로 옮겼다

`PIO_USB_DEFAULT_CONFIG` 는 **pio0 의 SM 0·1·2 를 전부 쓴다** (TX / RX / EOP) + DMA ch0.
01단계의 WS2812 가 pio0 sm0 이라 정면 충돌한다.

PIO USB 는 검증된 기본 설정을 유지하고 **WS2812 를 pio1 로 옮겼다** (`hw_def.h`).

### 코어 분배

```
core0 : tud_task() + CLI + LED      (ap.c)
core1 : tuh_task() 만               (usbh.c)
```

SOF 인터럽트를 core1 에서 돌리려면 **알람풀도 core1 에서 만들어야 한다.**

```c
config.alarm_pool = (void *)alarm_pool_create(HW_USBH_ALARM_NUM, 1);
tuh_configure(HW_USBH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &config);
tuh_init(HW_USBH_RHPORT);
```

하드웨어 알람 2번을 쓴다 (SDK 기본 알람풀은 3번이라 안 겹친다).

### 코어 간 리포트 전달

`tuh_hid_report_received_cb()` 는 core1 에서 불린다. CLI 는 core0 이다.
`pico/util/queue.h` 의 `queue_t` 를 쓴다 — 스핀락 기반이라 코어 간에 안전하다.

**core0 이 안 가져가면 버린다.** 여기서 막히면 USB 타이밍이 깨진다.
버린 개수는 `usbh info` 의 `drop` 으로 본다.

### 파일 배치

`baram-kbd-tester` 관례를 따랐다 — `src/hw/driver/usbh/` 아래, **`common/` 은 건드리지 않는다.**

```
src/hw/driver/usbh/
├── usbh.c / usbh.h              core1 태스크, PIO USB 설정, CLI
└── usbh_hid/usbh_hid.c / .h     mount / umount / report 콜백, 큐
```

## 구현 항목

- [x] `firm-sdk/Pico-PIO-USB` 서브모듈 @ 0.7.2
- [x] `PICO_PIO_USB_PATH` + `tinyusb_host` / `tinyusb_pico_pio_usb` 링크
- [x] WS2812 를 pio1 로 이동
- [x] `tusb_config.h` — host 설정
- [x] `usbh.c` — core1 태스크, 알람풀, 진단 CLI
- [x] `usbh_hid.c` — mount / umount / report 콜백, 리포트 큐
- [x] CLI `usbh info` / `usbh dump`

## 완료 판정

실기(HHKB Lite 2)에서 확인했다.

```
usbh info
core1     : running
D+ / D-   : GPIO12 / GPIO13
configure : 1
tuh_init  : 1
connect st: 1
speed     : full
frame num : 5427
mounted   : 1
connected : 1
rx / drop : 8 / 0
  [0] addr 1  04FE:0006  keyboard
```

`usbh dump` 로 실제 키코드 확인:

```
i0 p1 len 8 : 00 00 04 16 07 09 00 00     <- a s d f
i0 p1 len 8 : 00 00 0D 0E 0F 33 00 00     <- j k l ;
i0 p1 len 8 : 00 00 01 01 01 01 01 01     <- ErrorRollOver (6키 초과)
```

## 도중에 잡은 것

### 허브를 켜야 했다 — 열린 질문이 바로 걸렸다

처음엔 `CFG_TUH_HUB 0` 으로 두고 "08단계에서 판단" 이라고 미뤘다.
그런데 **HHKB Lite 2 는 뒷면에 USB 포트가 있는 허브 내장 키보드**라
`connect st: 1` 에 SOF 도 도는데 열거가 끝나지 않았다.

```c
#define CFG_TUH_HUB               1
#define CFG_TUH_DEVICE_MAX        (3 * CFG_TUH_HUB + 1)
```

켜자마자 바로 잡혔다. **허브 지원은 선택이 아니다** — 키보드 안에 허브가 있는 경우가 흔하다.

### 진단 없이는 못 찾았다

`connected : 0` 만 보고는 어디가 문제인지 알 수 없었다. 단계별로 값을 노출하고 나서야
"스택은 떴고(`configure`/`tuh_init` = 1), 연결도 봤고(`connect st: 1`),
SOF 도 돈다(`frame num` 증가). 그런데 열거가 안 끝난다" 로 좁혀졌고,
그 지점에서 허브가 후보로 떠올랐다.

`usbh info` 의 진단 항목은 그대로 남겨 둔다. 04단계 이후에도 쓸 일이 있다.

### raw GPIO 읽기는 신뢰할 수 없다 (제거함)

디버깅 중 `gpio_get(D+)` / `gpio_get(D-)` 를 CLI 에 찍었는데 **오해를 불렀다.**

- Pico-PIO-USB 는 읽은 값을 **반전**해서 쓴다 (`PORT_PIN_FS_IDLE = 0b01`)
- RP2350-E9 errata 때문에 라이브러리는 읽기 직전에 IE 를 껐다 켠다.
  core0 에서 그냥 `gpio_get()` 하면 그 절차를 안 거친다

실제로 이 값을 보고 "핀이 반대다" 라고 잘못 판단해서 한 번 헛돌았다.
**권위 있는 값은 `hcd_port_speed_get()` 과 열거 성공 여부다.** 그래서 raw 출력은 지웠다.

## 뒤이어 넣은 것 — 리포트 소비자와 LED 상태 표시

이 단계 직후에 발견한 것: **리포트 큐를 비우는 곳이 `usbh dump` 뿐이었다.**
평소에는 큐가 가득 차서 계속 버려지고 있었다 (`rx / drop : 689 / 657`).

`ap.c` 의 메인 루프에 소비자를 넣어 해결했다. 이 소비자는
04단계에서 PC 로 넘기는 자리가 되고, 05단계에서 `link/` 로 대체된다.

같이 `src/ap/led_status.c` 를 추가해 LED 로 상태를 표시한다
(→ [08-finalize.md](08-finalize.md#ws2812-상태-인디케이터--기본은-03단계-뒤에-넣었다)).
키 눌림 판정은 **직전 리포트와 비교해 새로 생긴 키코드**가 있을 때만 한다 —
이 키보드는 폴링마다 리포트를 올려서 그냥 세면 계속 반짝인다.

## 열린 질문

| 항목 | 내용 |
|---|---|
| boot vs report protocol | 지금은 장치가 주는 대로 받는다. HHKB 는 boot(8바이트)로 온다. NKRO 를 살리려면 report protocol + 리포트 디스크립터 파싱이 필요하다. 05단계에서 판단 |
| 유휴 리포트가 많다 | 키를 안 눌러도 폴링마다 `00 00 ...` 이 올라온다. 05단계에서 직전 리포트와 같으면 버린다 |
| 여러 키보드 동시 연결 | 허브를 켰으므로 가능해졌다. 비트맵을 어떻게 합칠지는 05단계 |
| 저속(LS) 키보드 | 아직 시험 못 했다. 라이브러리는 지원한다 (`low_speed` 처리 있음) |
| `drop` 이 생기는 조건 | 메인 루프가 큐를 비우게 한 뒤로는 0 이다. QMK 가 붙어 core0 이 바빠지면 다시 본다 |
