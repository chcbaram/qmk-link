# 03-usb-host — USB-A 에 꽂힌 키보드를 읽는다

**상태: ⬜ 미착수** — 구현 상세는 착수할 때 채운다.

## 목표

USB-A(J1) 에 일반 USB 키보드를 꽂으면 **CLI 에 HID report 가 찍힌다.**

## 배경 / 근거

**이 프로젝트에서 가장 위험한 단계다.** PIO USB 는 타이밍에 민감하고,
하드웨어 쪽 미확인 항목(R10/R13 풀업)은 1단계에서 미리 잡아 두었다 — R13 제거 완료, 핀 배정 확증.
여기가 안 되면 프로젝트 전제가 무너지므로 QMK 를 얹기 전에 최대한 앞에서 검증한다.

02단계에서 물려받는 것: **CDC 로그** — 이게 없으면 이 단계는 디버깅이 불가능하다.

## 설계

→ [usb-stack.md](usb-stack.md) 전체가 이 단계의 근거다.

### 핀

| | GPIO | 근거 |
|---|---|---|
| D+ | **12** | [hardware.md](hardware.md#usb-a-d--gpio12-d--gpio13) |
| D− | **13** | `DM = DP + 1` — Pico-PIO-USB 요구사항 만족 |

### 구성

```
RHPort 1 = PIO USB → OPT_MODE_HOST | OPT_MODE_FULL_SPEED
CFG_TUH_ENABLED     1
CFG_TUH_RPI_PIO_USB 1
CFG_TUH_HID         4
BOARD_TUH_RHPORT    1
```

`Pico-PIO-USB` 는 pico-sdk 에 없다 → `firm-sdk/Pico-PIO-USB` 서브모듈로 추가하고
SDK 의 `lib/tinyusb/src/portable/raspberrypi/pio_usb/hcd_pio_usb.c` 와 함께 빌드한다.

### 코어 분배

```
core0 : CLI + LED
core1 : tuh_task()   ← PIO USB 전용
```

`multicore_launch_core1()`. 코어 간 전달은 `qbuffer`.

### 클럭

이미 01단계에서 120MHz 로 맞춰 두었다. 여기서 건드릴 게 없다.

## 구현 항목

- [x] ~~실물에서 R10 / R13 실장 여부 확인~~ → R13 실장 확인, **제거 완료**
- [x] ~~진단 펌웨어로 검증~~ → 빈 포트 초록, FS 키보드 연결 시 빨강. **제거 확인 + 핀 배정 확증**
- [ ] `firm-sdk/Pico-PIO-USB` 서브모듈 추가
- [ ] `src/CMakeLists.txt` — pio_usb 소스 · include · `PICO_PIO_USB_PATH`
- [ ] `tusb_config.h` — host 설정 추가
- [ ] `hw/driver/usb/usbh.c` — `usbhInit()`, core1 태스크
- [ ] `tuh_hid_mount_cb` / `tuh_hid_umount_cb` / `tuh_hid_report_received_cb`
- [ ] CLI `usbh info` — 연결된 장치 · 인터페이스 · 리포트 덤프

## 완료 판정

1. 키보드를 꽂으면 CLI 에 `mount` 로그 (VID/PID, 인터페이스 수)
2. 키를 누르면 8바이트 boot report 가 찍힌다
3. 뽑으면 `umount` 로그
4. 꽂았다 뺐다를 반복해도 열거가 계속 된다
5. 아무것도 안 꽂았을 때 장치가 연결된 것으로 보이지 **않는다** (R13 제거 확인)

## 열린 질문

| 항목 | 내용 |
|---|---|
| ~~R10 / R13~~ | **해소됨(조치 대기).** R10 미실장 / **R13 실장** 으로 확인. R13 을 제거해야 호스트가 동작한다. 같은 측정으로 `GPIO12 = D+` 도 확증되었다 → [hardware.md](hardware.md#r10--r13--usb-a-포트의-풀업-해결됨) |
| 허브 지원 | `CFG_TUH_HUB` 를 켤지. 켜면 `CFG_TUH_DEVICE_MAX` 도 올려야 하고 메모리를 더 쓴다 |
| boot vs report protocol | report protocol 이 NKRO 를 살리지만 리포트 디스크립터 파싱이 필요하다. 우선 boot 로 시작하고 05단계에서 판단 |
| VBUS 타이밍 | J1 VBUS 는 VSYS 직결이라 전원 스위칭이 없다. 부팅 시점에 이미 전원이 올라가 있는지 실측 |
| 저속(LS) 키보드 | 오래된 키보드는 low-speed 다. Pico-PIO-USB 가 처리하는지 실측.<br>**R13 을 제거하지 않으면 LS 는 원리적으로 불가능하다** — D+ 가 항상 HIGH 라 속도 판별이 깨진다 |


---

## 부록 — USB-A 풀업 진단 펌웨어

R13 을 제거한 뒤 다시 확인할 때 쓴다. `src/ap/ap.c` 를 잠시 이걸로 바꿔 굽고,
확인이 끝나면 원래 점멸로 되돌린다. CDC 가 없어도 색으로 결과를 읽을 수 있다.

GPIO12 / GPIO13 을 입력 + 내부 풀다운(~50~80kΩ)으로 두고 읽는다.
외부 1.5K 풀업이 있으면 내부 풀다운을 이겨서 HIGH 가 된다.

| GPIO12 (D+) | GPIO13 (D−) | 색 | 의미 |
|---|---|---|---|
| LOW | LOW | 🟢 초록 | 풀업 없음 — **호스트 OK** |
| HIGH | LOW | 🔴 빨강 | D+ 풀업 (R13 실장) |
| LOW | HIGH | 🔵 파랑 | D− 풀업 (R10 실장) |
| HIGH | HIGH | ⚪ 흰색 | 둘 다 |

USB-A 에 장치를 꽂으면 그 장치의 풀업이 보이므로,
**꽂았을 때 색이 바뀌는지로 핀 배정까지 같이 확인**할 수 있다
(full-speed 장치 → 빨강, low-speed 장치 → 파랑).

```c
#include "ap.h"

#define USB_HOST_DP_PIN   QMK_LINK_USB_HOST_DP_PIN   // 12
#define USB_HOST_DM_PIN   QMK_LINK_USB_HOST_DM_PIN   // 13

static bool readPin(uint32_t pin)
{
  uint32_t high_cnt = 0;

  for (int i=0; i<16; i++)
  {
    if (gpio_get(pin) == true) high_cnt++;
    busy_wait_us(50);
  }
  return (high_cnt > 8);
}

void apInit(void)
{
  gpio_init(USB_HOST_DP_PIN);
  gpio_set_dir(USB_HOST_DP_PIN, GPIO_IN);
  gpio_pull_down(USB_HOST_DP_PIN);

  gpio_init(USB_HOST_DM_PIN);
  gpio_set_dir(USB_HOST_DM_PIN, GPIO_IN);
  gpio_pull_down(USB_HOST_DM_PIN);

  delay(10);
}

void apMain(void)
{
  uint32_t pre_time = millis();
  uint32_t color_pre = 0xFFFFFFFF;

  while(1)
  {
    if (millis()-pre_time >= 100)
    {
      bool     dp;
      bool     dm;
      uint32_t color;

      pre_time = millis();

      dp = readPin(USB_HOST_DP_PIN);
      dm = readPin(USB_HOST_DM_PIN);

      if      (dp == false && dm == false) color = WS2812_RGB(  0,  24,   0);
      else if (dp == true  && dm == false) color = WS2812_RGB( 24,   0,   0);
      else if (dp == false && dm == true ) color = WS2812_RGB(  0,   0,  24);
      else                                 color = WS2812_RGB( 24,  24,  24);

      if (color != color_pre)
      {
        color_pre = color;
        ws2812SetColor(0, color);
        ws2812Refresh();
      }
    }
  }
}
```
