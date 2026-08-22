#include "led.h"


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




bool ledInit(void)
{
  for (int i=0; i<LED_MAX_CH; i++)
  {
    ledOff(i);
  }

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

#endif
