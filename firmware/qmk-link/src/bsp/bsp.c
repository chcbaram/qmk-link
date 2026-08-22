#include "bsp.h"


// Pico-PIO-USB 는 clk_sys 가 120MHz 의 배수여야 한다.
// RP2350 기본값 150MHz 로는 동작하지 않으므로 부팅 직후 바꾼다.
// 3단계(USB host)에서 필요하지만, 나중에 바꾸면 그 사이 초기화된
// PIO / USB 클럭이 어긋나므로 1단계부터 여기서 확정한다.
#define BSP_SYS_CLOCK_KHZ     120000


bool bspInit(void)
{
  if (set_sys_clock_khz(BSP_SYS_CLOCK_KHZ, false) != true)
  {
    // 요청한 클럭을 못 맞추면 SDK 기본값으로 계속 간다.
    // 이 상태로는 PIO USB host 가 동작하지 않는다.
    return false;
  }

  return true;
}

void delay(uint32_t time_ms)
{
  sleep_ms(time_ms);
}

uint32_t millis(void)
{
  return to_ms_since_boot(get_absolute_time());
}

uint32_t micros(void)
{
  return to_us_since_boot(get_absolute_time());
}
