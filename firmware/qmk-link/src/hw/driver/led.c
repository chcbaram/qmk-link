#include "led.h"
#include "cli.h"


#ifdef _USE_HW_LED
#include "ws2812.h"


// 이 보드(RP2350-USB-A)에는 단순 GPIO LED 가 없다.
// WS2812B-0807 하나(L1, GPIO16)가 전부라서 ledOn/ledOff/ledToggle 은
// ws2812 드라이버 위에 얹는다. 호출부는 기존 _DEF_LED1 관용구를 그대로 쓴다.

typedef struct
{
  uint8_t   ws_ch;
  uint32_t  on_color;
} led_tbl_t;


static const led_tbl_t led_tbl[LED_MAX_CH] =
    {
        {0, WS2812_RGB(0, 16, 0)},    // 어두운 초록
    };

static bool led_state[LED_MAX_CH];


#ifdef _USE_HW_CLI
static void cliCmd(cli_args_t *args);
#endif




bool ledInit(void)
{
  for (int i=0; i<LED_MAX_CH; i++)
  {
    ledOff(i);
  }

#ifdef _USE_HW_CLI
  cliAdd("led", cliCmd);
#endif

  return true;
}

void ledOn(uint8_t ch)
{
  if (ch >= LED_MAX_CH) return;

  ws2812SetColor(led_tbl[ch].ws_ch, led_tbl[ch].on_color);
  ws2812Refresh();
  led_state[ch] = true;
}

void ledOff(uint8_t ch)
{
  if (ch >= LED_MAX_CH) return;

  ws2812SetColor(led_tbl[ch].ws_ch, 0);
  ws2812Refresh();
  led_state[ch] = false;
}

void ledToggle(uint8_t ch)
{
  if (ch >= LED_MAX_CH) return;

  if (led_state[ch] == true)
    ledOff(ch);
  else
    ledOn(ch);
}


#ifdef _USE_HW_CLI
void cliCmd(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 2 && args->isStr(0, "on"))
  {
    uint8_t ch = constrain(args->getData(1), 0, LED_MAX_CH-1);
    ledOn(ch);
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "off"))
  {
    uint8_t ch = constrain(args->getData(1), 0, LED_MAX_CH-1);
    ledOff(ch);
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "toggle"))
  {
    uint8_t ch = constrain(args->getData(1), 0, LED_MAX_CH-1);
    for (int i=0; i<10; i++)
    {
      ledToggle(ch);
      delay(200);
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("led on     CH[0~%d]\n", LED_MAX_CH-1);
    cliPrintf("led off    CH[0~%d]\n", LED_MAX_CH-1);
    cliPrintf("led toggle CH[0~%d]\n", LED_MAX_CH-1);
  }
}
#endif

#endif
