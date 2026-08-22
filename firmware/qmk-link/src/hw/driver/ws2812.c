#include "ws2812.h"


#ifdef _USE_HW_WS2812
#include "ws2812.pio.h"


typedef struct
{
  uint8_t     max_cnt;
  ws_color_t  rgb[WS2812_MAX_CH];
} ws2812_t;


static ws2812_t ws2812;
static PIO pio = HW_WS2812_PIO;




bool ws2812Init(void)
{
  uint offset;

  ws2812.max_cnt = WS2812_MAX_CH;
  for (int i=0; i<WS2812_MAX_CH; i++)
  {
    ws2812.rgb[i].data = 0;
  }

  offset = pio_add_program(pio, &ws2812_program);
  ws2812_program_init(pio, HW_WS2812_SM, offset, HW_WS2812_PIN, 800000, false);

  ws2812Refresh();

  return true;
}

bool ws2812SetColor(uint8_t ch, uint32_t rgb)
{
  if (ch >= WS2812_MAX_CH) return false;

  ws2812.rgb[ch].data = rgb;

  return true;
}

bool ws2812Refresh(void)
{
  for (int i=0; i<ws2812.max_cnt; i++)
  {
    ws_color_t color = ws2812.rgb[i];
    uint32_t   wire;

    // PIO 는 상위 24비트를 MSB 부터 밀어낸다.
    // 어느 색이 먼저 나가야 하는지는 LED 부품마다 다르므로 hw_def.h 가 정한다.
#ifdef HW_WS2812_ORDER_RGB
    wire = ((uint32_t)color.rgb.r << 16) |
           ((uint32_t)color.rgb.g <<  8) |
           ((uint32_t)color.rgb.b <<  0);
#else
    // 표준 WS2812B : G,R,B
    wire = ((uint32_t)color.rgb.g << 16) |
           ((uint32_t)color.rgb.r <<  8) |
           ((uint32_t)color.rgb.b <<  0);
#endif

    pio_sm_put_blocking(pio, HW_WS2812_SM, wire << 8);
  }

  return true;
}

#endif
