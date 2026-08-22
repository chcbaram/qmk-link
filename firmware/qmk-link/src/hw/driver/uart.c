#include "uart.h"
#include "cli.h"
#include "usb.h"

#ifdef _USE_HW_UART
#include "tusb.h"


// 이 보드에는 디버그 UART 핀이 없다. GPIO0/1 도 헤더로 나오지 않는다.
// 그래서 UART 채널은 USB CDC 하나뿐이고, 물리 UART 경로는 아예 없다.
// cli.c / log.c 가 채널 기반 API 를 쓰므로 인터페이스만 그대로 유지한다.

typedef struct
{
  bool      is_open;
  uint32_t  baud;
} uart_tbl_t;


static bool is_init = false;
static uart_tbl_t uart_tbl[UART_MAX_CH];


#ifdef _USE_HW_CLI
static void cliCmd(cli_args_t *args);
#endif




bool uartInit(void)
{
  for (int i=0; i<UART_MAX_CH; i++)
  {
    uart_tbl[i].is_open = false;
    uart_tbl[i].baud    = 115200;
  }

  is_init = true;

#ifdef _USE_HW_CLI
  cliAdd("uart", cliCmd);
#endif

  return true;
}

bool uartIsInit(void)
{
  return is_init;
}

bool uartOpen(uint8_t ch, uint32_t baud)
{
  if (ch >= UART_MAX_CH) return false;

  switch(ch)
  {
    case HW_UART_CH_USB:
      uart_tbl[ch].baud    = baud;
      uart_tbl[ch].is_open = true;
      break;

    default:
      return false;
  }

  return uart_tbl[ch].is_open;
}

bool uartIsOpen(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return false;

  return uart_tbl[ch].is_open;
}

bool uartClose(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return false;

  uart_tbl[ch].is_open = false;
  return true;
}

uint32_t uartAvailable(uint8_t ch)
{
  uint32_t ret = 0;

  if (ch >= UART_MAX_CH) return 0;

  switch(ch)
  {
    case HW_UART_CH_USB:
      ret = tud_cdc_available();
      break;
  }

  return ret;
}

bool uartFlush(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return false;

  switch(ch)
  {
    case HW_UART_CH_USB:
      while(tud_cdc_available())
      {
        tud_cdc_read_char();
      }
      break;
  }

  return true;
}

uint8_t uartRead(uint8_t ch)
{
  uint8_t ret = 0;

  if (ch >= UART_MAX_CH) return 0;

  switch(ch)
  {
    case HW_UART_CH_USB:
      ret = tud_cdc_read_char();
      break;
  }

  return ret;
}

uint8_t uartReadBuf(uint8_t ch, uint8_t *p_buf, uint32_t length)
{
  uint32_t index = 0;

  if (ch >= UART_MAX_CH) return 0;

  while(index < length)
  {
    if (uartAvailable(ch) == 0)
    {
      usbUpdate();
      continue;
    }
    p_buf[index] = uartRead(ch);
    index++;
  }

  return index;
}

uint32_t uartWrite(uint8_t ch, uint8_t *p_data, uint32_t length)
{
  uint32_t ret = 0;

  if (ch >= UART_MAX_CH) return 0;

  switch(ch)
  {
    case HW_UART_CH_USB:
      // CDC FIFO 는 작다. 한 번에 다 못 넣으면 나눠서 넣고
      // 그 사이 usbUpdate() 로 호스트가 가져가게 한다.
      // 호스트가 안 붙어 있으면 버린다 (로그 때문에 멈추면 안 된다).
      if (tud_cdc_connected() != true)
      {
        ret = length;
        break;
      }

      while (ret < length)
      {
        uint32_t wr_len;

        wr_len = tud_cdc_write(&p_data[ret], length - ret);
        if (wr_len == 0)
        {
          if (tud_cdc_connected() != true) break;
          usbUpdate();
          continue;
        }
        ret += wr_len;
        tud_cdc_write_flush();
      }
      break;
  }

  return ret;
}

uint32_t uartPrintf(uint8_t ch, const char *fmt, ...)
{
  char buf[256];
  va_list args;
  int len;
  uint32_t ret = 0;

  va_start(args, fmt);
  len = vsnprintf(buf, 256, fmt, args);

  if (len > 0)
  {
    ret = uartWrite(ch, (uint8_t *)buf, (uint32_t)len);
  }

  va_end(args);

  return ret;
}

uint32_t uartGetBaud(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;

  return uart_tbl[ch].baud;
}

bool uartSetBaud(uint8_t ch, uint32_t baud)
{
  if (ch >= UART_MAX_CH) return false;

  // CDC 는 보레이트가 의미 없다. 값만 기억한다.
  uart_tbl[ch].baud = baud;
  return true;
}


#ifdef _USE_HW_CLI
void cliCmd(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    for (int i=0; i<UART_MAX_CH; i++)
    {
      cliPrintf("_DEF_UART%d : %s, %d bps\n",
                i + 1,
                uartIsOpen(i) ? "open  " : "closed",
                uartGetBaud(i));
    }
    cliPrintf("cdc connected : %d\n", tud_cdc_connected());
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("uart info\n");
  }
}
#endif

#endif
