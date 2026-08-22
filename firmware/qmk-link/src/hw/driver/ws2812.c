#include "ws2812.h"
#include "cli.h"


#ifdef _USE_HW_WS2812
#include "ws2812.pio.h"


typedef struct
{
  uint8_t     max_cnt;
  ws_color_t  rgb[WS2812_MAX_CH];
} ws2812_t;


static ws2812_t ws2812;
static PIO pio = HW_WS2812_PIO;


#ifdef _USE_HW_CLI
static void cliCmd(cli_args_t *args);
#endif




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

#ifdef _USE_HW_CLI
  cliAdd("ws2812", cliCmd);
#endif

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


#ifdef _USE_HW_CLI
void cliCmd(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 5 && args->isStr(0, "set"))
  {
    uint8_t ch;
    ws_color_t rgb;

    ch = constrain(args->getData(1), 0, WS2812_MAX_CH-1);
    memset(&rgb, 0, sizeof(rgb));
    rgb.rgb.r = args->getData(2);
    rgb.rgb.g = args->getData(3);
    rgb.rgb.b = args->getData(4);

    ws2812SetColor(ch, rgb.data);
    ws2812Refresh();
    cliPrintf("ch%d r%d g%d b%d\n", ch, rgb.rgb.r, rgb.rgb.g, rgb.rgb.b);
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "test"))
  {
    uint8_t ch;
    ws_color_t rgb[3];
    const char *name[3] = {"red", "green", "blue"};

    ch = constrain(args->getData(1), 0, WS2812_MAX_CH-1);
    memset(&rgb, 0, sizeof(rgb));
    rgb[0].rgb.r = 64;
    rgb[1].rgb.g = 64;
    rgb[2].rgb.b = 64;

    // 전송 순서가 맞는지 눈으로 확인하는 용도다.
    // 이 보드는 R,G,B 순이라 hw_def.h 의 HW_WS2812_ORDER_RGB 로 잡아 뒀다.
    for (int i=0; i<3; i++)
    {
      cliPrintf("%s\n", name[i]);
      ws2812SetColor(ch, rgb[i].data);
      ws2812Refresh();
      delay(700);
    }
    ws2812SetColor(ch, 0);
    ws2812Refresh();

    ret = true;
  }

  if (ret != true)
  {
    cliPrintf("ws2812 set CH[0~%d] R[0~255] G[0~255] B[0~255]\n", WS2812_MAX_CH-1);
    cliPrintf("ws2812 test CH[0~%d]\n", WS2812_MAX_CH-1);
  }
}
#endif

#endif
