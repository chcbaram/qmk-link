# 02-cli-cdc — 관측 수단을 만든다

**상태: ✅ 완료**

## 목표

Type-C 에 **USB CDC** 를 올려 CLI 와 로그를 쓴다. 그리고 `flash.py` 가
BOOTSEL 버튼 없이 굽게 만든다.

## 배경 / 근거

**이 보드에는 디버그 UART 핀이 없다.** GPIO0/1 도 헤더로 나오지 않는다.
그래서 CDC 가 **유일한 관측 수단**이다.

03단계(PIO USB host)는 타이밍과 하드웨어가 얽혀 있어 로그 없이 짜면 눈을 감고 짜는 셈이다.
그래서 USB host 보다 CDC 를 먼저 한다.

01단계에서 물려받는 것: 120MHz 클럭, `hw_def.h` 의 `_USE_HW_*` 구조, `flash.py` 골격.

## 설계

### `pico_stdio_usb` 를 쓰지 않는다

자체 device descriptor 를 갖고 있어 04단계의 HID descriptor 와 공존할 수 없다.
지금부터 우리 descriptor 를 직접 만들고, 04단계에서 여기에 HID 를 추가한다.
→ [usb-stack.md](usb-stack.md#pico_stdio_usb-를-쓰지-않는-이유)

이번 단계의 구성:

```
IAD ─ CDC (comm + data)     CLI · 로그
Vendor RESET                picotool 호환 (pico_usb_reset 라이브러리가 처리)
```

VID/PID 는 개발용 임시값 `0x2E8A:0xF001`. 08단계에서 확정한다.

### UART 계층을 그대로 두고 CDC 를 채널로 붙인다

`cli.c` / `log.c` 는 채널 기반 UART API 위에 있다. 이 구조를 바꾸지 않고
`uart.c` 를 CDC 전용으로 다시 썼다 (`HW_UART_MAX_CH 1`, `_DEF_UART1` = CDC).
물리 UART 경로는 아예 없앴다 — 이 보드엔 핀이 없다.

`uartWrite()` 는 호스트가 붙어 있지 않으면 **버린다.** 로그 때문에 펌웨어가 멈추면 안 된다.
FIFO 가 차면 `usbUpdate()` 를 돌려 호스트가 가져가게 하고 이어서 쓴다.

### BOOTSEL 진입 경로 네 가지

| 경로 | 구현 | 펌웨어가 죽어도 되나 |
|---|---|---|
| **CDC 1200bps touch** | `pico_usb_reset` (`PICO_ENABLE_USB_RESET_VIA_BAUD_RATE=1`) | ✗ |
| **Key2 더블클릭** | `pico_bootsel_via_double_reset` | ✗ |
| Key1(BOOTSEL) + Key2(Reset) | 하드웨어 | ✅ |
| CLI `reset boot` | `reset.c` | ✗ |

## 구현 항목

- [x] `hw_def.h` — `_USE_HW_USB / UART / CLI / CLI_GUI / LOG / SWTIMER / RESET`
- [x] `hw/driver/usb/tusb_config.h` — `CFG_TUD_CDC 1`
- [x] `hw/driver/usb/usbd_desc.c` — IAD + CDC + vendor RESET, 고유 시리얼(`pico_unique_id`)
- [x] `hw/driver/usb/usb.c` — `usbInit()` / `usbUpdate()` / CLI `usb`
- [x] `hw/driver/uart.c` — CDC 전용으로 재작성
- [x] `hw/driver/cli.c`, `cli_gui.c`, `log.c`, `common/core/qbuffer.c` (hola-mini 에서)
- [x] `hw/driver/swtimer.c` — `add_repeating_timer_us(-1000, ...)` 로 1ms 틱
- [x] `hw/driver/reset.c` — 리셋 사유(`POWMAN_CHIP_RESET` HAD_* 비트) + CLI `reset`
      헤더는 nano-ch32h417 의 `reset.h` 를 그대로 쓰고, **실제로 쓰는 것만 구현**했다
      (`resetSetBits` · `resetSetBootMode` · `resetGetBootMode` 는 쓰는 곳이 없어 미구현)
- [x] CLI 명령: `usb` `uart` `log` `ws2812` `led` `reset`
- [x] `bsp.c` 의 `delay()` 가 `cliLoopIdle()` 을 돌린다 — 백그라운드 처리의 유일한 훅
- [ ] `firm-sdk/tools/list_ports.py` — 아직. `flash.py --list` 로 충분해서 미룸

## 완료 판정

전부 실기에서 확인했다.

1. ✅ Type-C 연결 시 CDC 포트 하나 (`2E8A:F001 QMK-LINK`)
2. ✅ CLI 프롬프트와 명령 목록
3. ✅ **`Clk sys : 120000000 Hz`** — 01단계 이월 항목 해소
4. ✅ `ws2812 set` / `led` 로 LED 제어
5. ✅ **BOOTSEL 을 누르지 않고** `flash.py` 로 다운로드 (1200bps touch)
6. ✅ **Key2 더블클릭 → BOOTSEL**
7. ✅ `log boot` 로 부팅 로그 재생

## 도중에 잡은 것

### 오래 도는 CLI 명령에서 USB 가 멎는다 → `delay()` 가 훅이다

다른 프로젝트는 CLI 가 **하드웨어 UART** 위에 있다. 메인 루프가 멈춰도
페리페럴 FIFO / 인터럽트가 돌아서 송수신이 유지된다.
이 보드는 CLI 가 **USB CDC** 위에 있고 `tud_task()` 가 메인 루프에서만 돈다.
`ws2812 test` 처럼 몇 초씩 도는 명령에 들어가면 USB 가 통째로 멎는다.

처음에는 `cli.c` 를 고쳐서 풀려고 했다 — `cliKeepLoop()` 에 `cliLoopIdle()` 을 넣고
`cliDelay()` 를 새로 만들었다. **되돌렸다.** `common/` 은 다른 프로젝트와 공유하는 자산이고,
이미 확립된 관례가 따로 있었다.

**NU87-TinyDK 는 `bsp.c` 의 `delay()` 안에서 `cliLoopIdle()` 을 돌린다.**

```c
void delay(uint32_t time_ms)
{
  uint32_t pre_time = millis();

  while (millis() - pre_time < time_ms)
  {
#ifdef _USE_HW_CLI
    cliLoopIdle();
#endif
  }
}
```

이러면 `delay()` 가 유일한 훅이 되고, **기존 코드를 한 줄도 안 고쳐도**
그 사이에 USB 처리가 돈다. 앱은 `cliLoopIdle()` 에 `usbUpdate()` 를 연결하기만 하면 된다.
`bsp.c` 는 `cli.h` 를 include 한다 (NU87 · nano-ch32h417 둘 다 그렇게 한다).

결과적으로 **`cli.c` · `cli.h` 는 hola-mini 원본과 바이트 단위로 동일하다.**

`ws2812 test` 가 도는 동안 `ap.c` 의 점멸이 색을 덮어쓰지 않는 것도 여기서 따라온다 —
CLI 명령이 도는 동안에는 `apMain()` 의 루프 자체가 진행되지 않는다.
(즉시 반환하는 `ws2812 set` 은 점멸에 덮인다. 정상이다.)

### CRLF 는 지원하지 않는다 (의도)


`CLI_KEY_ENTER` 는 CR(0x0D) 만 본다. LF(0x0A) 는 문자로 취급된다.
한때 LF 를 무시하도록 고쳤다가 **되돌렸다.** 측정해 보니 값어치가 없었다.

| | 원본 | LF 무시 패치 |
|---|---|---|
| CRLF 로 일반 명령 | 동작 (빈 줄 하나 에코) | 동작 |
| CRLF 로 keep-loop 명령 | **중단** | **똑같이 중단** |
| Ctrl+J | 안 보이는 문자 1개 입력 | 무시 |

`cliKeepLoop()` 은 `uartAvailable()` 로 "버퍼에 바이트가 있나" 만 본다.
CRLF 를 보내면 LF 는 명령이 도는 동안 **아직 안 읽힌 상태로 버퍼에 남아** 있고,
`cliUpdate()` 의 LF 처리는 명령이 끝난 뒤에야 실행된다.
그래서 LF 무시로는 keep-loop 중단이 안 고쳐진다.

**호스트 도구는 CR 만 보내면 된다.** `screen` · `miniterm` · PuTTY 는 원래 그렇게 한다.
CRLF 까지 지원하려면 `cliKeepLoop()` 이 버퍼의 LF 를 읽어 버려야 하는데,
공유 파일과 더 벌어지고 루프를 깨는 키 입력이 소비되는 부작용이 생긴다.

### 1200bps touch — DTR 을 보면 안 된다

처음엔 "1200bps **이고** DTR 이 내려감" 으로 판정했다. Arduino 관례가 그래서였는데,
호스트 라이브러리(pyserial 등)가 **포트를 열 때 DTR 을 먼저 세우기 때문에**
`SET_LINE_CODING` 시점에는 DTR 이 이미 1 이라 조건이 성립하지 않았다.

pico-sdk 는 DTR 을 아예 안 본다. 보레이트만 본다.
→ 자체 콜백을 버리고 `pico_usb_reset` 라이브러리를 쓴다.

### 리셋 더블클릭 — RP2350 은 RAM 에 매직을 못 남긴다

직접 구현하려다 실패했다. **RP2350 은 RUN 핀 리셋 때 RAM 전원이 내려간다.**
실측으로 확인했다:

| 저장소 | 리셋 후 |
|---|---|
| `.uninitialized_data` (SRAM) | 지워짐 |
| watchdog `scratch[0..3]` | 지워짐 |
| POWMAN `scratch[0]` | 지워짐 |

업스트림에도 같은 문제가 보고되어 있다
([pico-sdk#2043](https://github.com/raspberrypi/pico-sdk/issues/2043)) —
부트롬이 SRAM 을 의사난수로 채운다.

RP2350 은 대신 **`POWMAN_CHIP_RESET` 의 `DOUBLE_TAP` 비트**를 갖고 있다.
RUN 을 넘겨 유지되도록 만들어진 전용 비트고, 쓰기에는 POWMAN 비밀번호가 필요하다
(내가 시험한 POWMAN *scratch* 와는 다른 레지스터다).

SDK 2.1.0 부터 `pico_bootsel_via_double_reset` 이 이걸 쓴다.
→ **직접 만들지 않고 라이브러리를 링크한다.** 우리 코드는 한 줄도 없다.

```cmake
target_link_libraries(${PRJ_NAME} pico_bootsel_via_double_reset)
target_compile_definitions(${PRJ_NAME} PRIVATE
  PICO_BOOTSEL_VIA_DOUBLE_RESET_TIMEOUT_MS=500)
```

대가는 **부팅이 500ms 늦어지는 것**이다 (그 창 동안 busy wait).
SDK 기본값은 200ms 인데 누르기 어려워서 늘렸다. 짧게 하려면 이 값을 줄인다.

## `common/` 을 건드리지 않는다

작업 도중 정한 규칙이다. `src/common/` 은 다른 baram 프로젝트와 공유하는 자산이라
**바꾸기 전에 먼저 확인받는다.** 여기서 갈라지면 프로젝트마다 같은 파일이 조금씩 달라진다.

이번 단계에서 되돌린 것:

| | 한때 넣었던 것 | 결론 |
|---|---|---|
| `cli.c` / `cli.h` | `cliDelay()` 추가, `cliKeepLoop()` 에 `cliLoopIdle()` 호출, LF 무시 | **전부 되돌림.** `delay()` 훅으로 해결 |
| `led.h` | `ledLock()` 추가 | **되돌림.** CLI 명령이 도는 동안 `apMain()` 이 멈추므로 애초에 필요 없었다 |
| `reset.h` | 이 보드용 축소판 | **nano-ch32h417 원본으로.** 구현만 필요한 만큼 한다 |

지금 `src/common/` 은 전부 원본과 바이트 단위로 동일하다.

## 열린 질문

| 항목 | 내용 |
|---|---|
| `clk_peri` 가 48MHz | `set_sys_clock_khz(120000)` 후 clk_peri 가 USB PLL(48MHz)에 물려 있다. 지금은 UART/SPI 하드웨어를 안 써서 무관하지만, 쓰게 되면 확인해야 한다 |
| `list_ports.py` | 아직 안 만들었다. `flash.py --list` 로 충분했다 |
| 부팅 초반 로그 | CDC 열거 전이라 화면에 안 나간다. `log boot` 로 꺼내 본다 |
| VID/PID | 임시값 `2E8A:F001`. 08단계에서 확정 |
