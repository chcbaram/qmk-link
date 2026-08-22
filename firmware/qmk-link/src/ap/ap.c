#include "ap.h"


void apInit(void)
{
  cliOpen(HW_UART_CH_CLI, 115200);
}

void apMain(void)
{
  uint32_t pre_time;

  pre_time = millis();

  while(1)
  {
    if (millis()-pre_time >= 500)
    {
      pre_time = millis();
      ledToggle(_DEF_LED1);
    }

    usbUpdate();
    cliMain();
  }
}

// cli 가 오래 걸리는 명령을 도는 동안에도 USB 는 살아 있어야 한다.
void cliLoopIdle(void)
{
  usbUpdate();
}
