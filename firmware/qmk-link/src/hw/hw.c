#include "hw.h"


bool hwInit(void)
{
  bspInit();

#ifdef _USE_HW_RESET
  resetInit();
#endif

#ifdef _USE_HW_CLI
  cliInit();
#endif

#ifdef _USE_HW_USB
  // CDC 가 유일한 로그 경로다. 다른 것보다 먼저 올린다.
  usbInit();
#endif

#ifdef _USE_HW_UART
  uartInit();
  uartOpen(HW_UART_CH_USB, 115200);
#endif

#ifdef _USE_HW_LOG
  logInit();
  logOpen(HW_LOG_CH, 115200);
#endif

  logPrintf("[ Firmware Begin... ]\r\n");
  logPrintf("Booting..Name \t\t: %s\r\n", _DEF_BOARD_NAME);
  logPrintf("Booting..Ver  \t\t: %s\r\n", _DEF_FIRMWATRE_VERSION);
  logPrintf("Clk sys  \t\t: %d Hz\r\n", clock_get_hz(clk_sys));
  logPrintf("Clk peri \t\t: %d Hz\r\n", clock_get_hz(clk_peri));
  logPrintf("Clk usb  \t\t: %d Hz\r\n", clock_get_hz(clk_usb));

#ifdef _USE_HW_RESET
  resetLog();
#endif

#ifdef _USE_HW_WS2812
  // led.c 가 ws2812 위에 올라가므로 반드시 ledInit() 보다 먼저다.
  ws2812Init();
#endif

#ifdef _USE_HW_LED
  ledInit();
#endif

#ifdef _USE_HW_SWTIMER
  swtimerInit();
#endif

#ifdef _USE_HW_FLASH
  flashInit();
#endif

#ifdef _USE_HW_USBH
  // core1 을 띄운다. 다른 초기화가 끝난 뒤에 한다.
  usbhInit();
#endif

#ifdef _USE_HW_LOG
  // 여기까지가 부팅이다. 이후 로그는 boot 버퍼에 쌓지 않는다.
  // CDC 는 호스트가 포트를 열어야 살아나므로 위 로그는 화면에 안 나간다.
  // 나중에 `log boot` 로 꺼내 본다.
  logBoot(false);
#endif

  return true;
}
