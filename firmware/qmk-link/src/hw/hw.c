#include "hw.h"


bool hwInit(void)
{
  bspInit();

#ifdef _USE_HW_WS2812
  // led.c 가 ws2812 위에 올라가므로 반드시 ledInit() 보다 먼저다.
  ws2812Init();
#endif

#ifdef _USE_HW_LED
  ledInit();
#endif

  return true;
}
