#include "led_status.h"


// 보드에 LED 가 WS2812 하나뿐이라 색과 주기로 상태를 표시한다.
//
//   빨강 빠른 점멸   PC 미연결
//   주황 느린 점멸   PC 는 붙었는데 키보드가 없다
//   초록 계속 켜짐   정상
//   흰색 짧게        키가 눌렸다 (최우선)
//
// 밝기는 낮게 잡는다. WS2812 는 값이 조금만 커도 눈이 아프다.

#define LED_KEY_FLASH_MS    30


typedef struct
{
  uint32_t color;
  uint16_t period_ms;
  uint16_t on_ms;
} led_pattern_t;


static const led_pattern_t led_pattern[LED_ST_MAX] =
    {
        [LED_ST_NO_PC ] = { WS2812_RGB(24,  0,  0),  400, 200 },
        [LED_ST_NO_KBD] = { WS2812_RGB(24, 10,  0), 1000, 500 },
        // on_ms 가 period_ms 이상이면 계속 켜진다.
        [LED_ST_READY ] = { WS2812_RGB( 0, 12,  0), 1000, 1000 },
    };

static const uint32_t led_key_color = WS2812_RGB(28, 28, 28);

static uint32_t key_time  = 0;
static bool     key_valid = false;
static uint32_t color_pre = 0xFFFFFFFF;




bool ledStatusInit(void)
{
  color_pre = 0xFFFFFFFF;
  return true;
}

led_status_t ledStatusGet(void)
{
#ifdef _USE_HW_USB
  if (usbIsConnect() != true)
  {
    return LED_ST_NO_PC;
  }
#endif

#ifdef _USE_HW_USBH
  if (usbhHidIsConnected() != true)
  {
    return LED_ST_NO_KBD;
  }
#endif

  return LED_ST_READY;
}

void ledStatusKeyEvent(void)
{
  key_time  = millis();
  key_valid = true;
}

void ledStatusUpdate(void)
{
  uint32_t now = millis();
  uint32_t color;

  if (key_valid == true && (now - key_time) < LED_KEY_FLASH_MS)
  {
    color = led_key_color;
  }
  else
  {
    const led_pattern_t *p_pattern = &led_pattern[ledStatusGet()];

    key_valid = false;

    // now 를 주기로 나눈 나머지를 쓰면 상태 변수 없이 패턴이 돈다.
    color = ((now % p_pattern->period_ms) < p_pattern->on_ms) ? p_pattern->color : 0;
  }

  if (color != color_pre)
  {
    color_pre = color;
    ws2812SetColor(0, color);
    ws2812Refresh();
  }
}
