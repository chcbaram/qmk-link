# 02-cli-cdc — 관측 수단을 만든다

**상태: ⬜ 미착수** — 구현 상세는 착수할 때 채운다.

## 목표

Type-C 에 **USB CDC** 를 올려 CLI 와 로그를 쓴다. 그리고 `flash.py` 가
BOOTSEL 버튼 없이 굽게 만든다.

- `cliMain()` 이 도는 CDC 콘솔
- `logPrintf()` / `logBoot()`
- `swtimer`
- **1200bps touch → `reset_usb_boot()`**
- CLI `reset boot`

## 배경 / 근거

**이 보드에는 디버그 UART 핀이 없다.** GPIO0/1 도 헤더로 나오지 않는다.
그래서 CDC 가 **유일한 관측 수단**이다.

03단계(PIO USB host)는 타이밍과 하드웨어 풀업 문제가 얽혀 있어
로그 없이 짜면 눈을 감고 짜는 셈이다. 그래서 USB host 보다 CDC 를 먼저 한다.

01단계에서 물려받는 것: 120MHz 클럭, `hw_def.h` 의 `_USE_HW_*` 구조, `flash.py` 골격.

## 설계

### `pico_stdio_usb` 를 쓰지 않는다

자체 device descriptor 를 갖고 있어 04단계의 HID descriptor 와 공존할 수 없다.
**지금부터 우리 descriptor 를 직접 만든다.** 04단계에서 여기에 HID 를 추가하는 구조다.

→ [usb-stack.md](usb-stack.md#pico_stdio_usb-를-쓰지-않는-이유)

### 이번 단계의 descriptor

```
IAD ─ CDC (comm + data)     CLI · 로그
Vendor (RESET)              picotool 호환용 (부수적)
```

HID 는 04단계에서 붙인다. 지금은 CDC 만.

### 1200bps touch

`tud_cdc_line_coding_cb()` 에서 `bit_rate == 1200` 이고 DTR 이 내려가면
`reset_usb_boot(0, 0)` 을 호출한다. pico-sdk 의
`PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE` 와 같은 동작이다.

이게 되면 `flash.py` 가 이미 구현해 둔 touch 경로가 살아난다
(01단계에서는 CDC 가 없어 항상 수동 BOOTSEL 로 떨어졌다).

**vendor RESET 인터페이스보다 1200bps touch 를 1순위로 둔다.**
vendor 쪽은 libusb 를 타서 Windows 에서 WinUSB 드라이버가 필요하다.

### 가져올 것

`~/hdd/git/baram-hola-mini/firmware/hola-mini/src/` 에서:

| 파일 | 비고 |
|---|---|
| `common/hw/include/{cli,cli_gui,log,cdc}.h` | 헤더 |
| `common/core/qbuffer.c/h` | 링버퍼. 03단계 코어 간 전달에도 쓴다 |
| `hw/driver/cli.c`, `cli_gui.c`, `log.c` | UART 의존을 CDC 로 바꾼다 |
| `hw/driver/usb/` | descriptor 는 새로 쓴다 |
| `ap/modules/qmk/port/platforms/bootloader.c` | `reset_usb_boot()` 래퍼 |

`~/hdd/git/NU87-TinyDK/firmware/firm-sdk/tools/list_ports.py` → `firm-sdk/tools/list_ports.py`
(VID/PID 를 이 보드에 맞춘다)

## 구현 항목

- [ ] `hw_def.h` — `_USE_HW_CDC`, `_USE_HW_CLI`, `_USE_HW_LOG`, `_USE_HW_SWTIMER`
- [ ] `hw/driver/usb/tusb_config.h` — `CFG_TUD_CDC 1`, `CFG_TUD_VENDOR 1`
- [ ] `hw/driver/usb/usbd_desc.c` — IAD + CDC + vendor RESET
- [ ] `hw/driver/usb/usb.c` — `usbInit()`, `tud_task()` 구동
- [ ] `hw/driver/cdc.c` — `cdcRead/cdcWrite/cdcAvailable`
- [ ] `hw/driver/cli.c`, `log.c`, `swtimer.c`
- [ ] `tud_cdc_line_coding_cb()` — 1200bps touch
- [ ] CLI 명령: `reset`, `led`, `ws2812`, `boot`
- [ ] `firm-sdk/tools/list_ports.py`
- [ ] `.vscode/tasks.json` 에 `console-monitor` 태스크

## 완료 판정

1. Type-C 연결 시 PC 에 CDC 포트가 하나 뜬다
2. 터미널로 붙으면 부팅 로그가 보이고 CLI 프롬프트가 뜬다
3. `clock_get_hz(clk_sys)` 가 **120,000,000** 으로 찍힌다 (01단계 이월 항목 해소)
4. `ws2812 test1 0` 으로 LED 색이 바뀐다
5. **BOOTSEL 을 누르지 않고** `python3 ../firm-sdk/tools/flash.py build/src/qmk-link.uf2` 가 성공한다
6. CLI 에서 `reset boot` → BOOTSEL 볼륨이 뜬다

## 열린 질문

| 항목 | 내용 |
|---|---|
| VID / PID | 개발용 임시값을 정한다. 08단계에서 확정 |
| CDC 가 없을 때 로그 | 부팅 초반 로그는 CDC 열거 전이라 못 본다. `logBoot()` 버퍼에 모았다가 연결 시 뱉는 hola-mini 방식을 쓴다 |
| Windows 드라이버 | CDC 단독이면 `usbser.sys` 가 자동으로 잡는다. 04단계에서 복합 장치가 되면 IAD 가 필요하다 |
