#include "usb.h"

#ifdef _USE_HW_USB
#include "tusb.h"
#include "cli.h"
#include "pico/bootrom.h"


extern void usbdDescInit(void);
extern void usbdDescSetProductId(uint16_t pid);
extern uint16_t usbdDescGetProductId(void);
#include "usbd_hid.h"

static bool is_init = false;

//-- PID 전환 (09단계)
//
// ★ 왜 끊었다 붙이나
//
//   PID 는 device descriptor 에 있고, 호스트는 그걸 **열거할 때 한 번만** 읽는다.
//   달아 놓은 채로 값만 바꾸면 아무 일도 일어나지 않는다. D+ 풀업을 내렸다
//   올려서 호스트가 새 장치로 다시 물어보게 해야 한다.
//
// ★ 왜 블록하지 않나
//
//   여기서 delay() 를 쓰면 cliLoopIdle() 이 다시 돌고, 그 안에 이 함수를 부르는
//   경로가 들어 있다 (usbdHidSendRaw 의 주석과 같은 함정이다).
//   millis() 로 세고 usbUpdate() 가 마저 진행시킨다.
//
//   끊긴 동안 CDC 도 같이 죽는다. 로그가 잠깐 끊기는 것은 정상이다.
#define USB_REENUM_GAP_MS   100

static uint32_t reenum_time  = 0;
static bool     is_reenum    = false;
static uint32_t reenum_count = 0;


#ifdef _USE_HW_CLI
static void cliCmd(cli_args_t *args);
#endif




bool usbInit(void)
{
  usbdDescInit();
  usbdHidInit();

  tud_init(BOARD_TUD_RHPORT);

  is_init = true;

#ifdef _USE_HW_CLI
  cliAdd("usb", cliCmd);
#endif

  return true;
}

void usbUpdate(void)
{
  if (is_init != true) return;

  if (is_reenum == true && (millis() - reenum_time) >= USB_REENUM_GAP_MS)
  {
    is_reenum = false;
    tud_connect();
  }

  tud_task();
}

void usbSetProductId(uint16_t pid)
{
  if (is_init != true)
  {
    // 아직 tud_init() 전이면 그냥 값만 넣는다. 처음 열거부터 이 값으로 나간다.
    usbdDescSetProductId(pid);
    return;
  }

  if (usbdDescGetProductId() == pid) return;

  usbdDescSetProductId(pid);

  logPrintf("[  ] usb PID -> %04X (재열거)\r\n", pid);

  tud_disconnect();
  reenum_time = millis();
  is_reenum   = true;
  reenum_count++;
}

uint16_t usbGetProductId(void)
{
  return usbdDescGetProductId();
}

bool usbIsOpen(void)
{
  return is_init;
}

bool usbIsConnect(void)
{
  return (tud_connected() && !tud_suspended());
}

bool usbIsSuspended(void)
{
  return tud_suspended();
}

void usbRebootBootsel(void)
{
  reset_usb_boot(0, 0);
}


//-- 1200bps touch
//
// 호스트가 포트를 1200bps 로 열면 BOOTSEL 로 재부팅한다.
// Arduino 이래의 관례이고, firm-sdk/tools/flash.py 가 이걸 쓴다.
//
// 콜백(tud_cdc_line_coding_cb)은 pico_usb_reset 라이브러리가 제공한다.
// PICO_ENABLE_USB_RESET_VIA_BAUD_RATE=1 로 켠다 (CMakeLists 참고).
//
// 처음에는 여기서 직접 구현하면서 DTR 이 내려간 것도 같이 봤는데,
// 호스트 라이브러리(pyserial 등)가 포트를 열 때 DTR 을 먼저 세우기 때문에
// 그 조건이 걸려서 동작하지 않았다. SDK 처럼 보레이트만 본다.
//
// vendor RESET 인터페이스도 같이 열려 있지만 그쪽은 libusb 를 타서
// Windows 에서 WinUSB 드라이버가 필요하다. 1200bps touch 가 1순위다.


#ifdef _USE_HW_CLI
void cliCmd(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("mounted   : %d\n", tud_mounted());
    cliPrintf("connected : %d\n", tud_connected());
    cliPrintf("suspended : %d\n", tud_suspended());
    cliPrintf("cdc conn  : %d\n", tud_cdc_connected());
    cliPrintf("hid ready : kbd %d  extra %d  raw %d\n",
              usbdHidIsReady(HID_ITF_KEYBOARD),
              usbdHidIsReady(HID_ITF_EXTRA),
              usbdHidIsReady(HID_ITF_RAW));
    cliPrintf("host led  : 0x%02X\n", usbdHidGetLed());
    cliPrintf("PID       : %04X  (재열거 %d 회)\n",
              usbGetProductId(), (int)reenum_count);
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "boot"))
  {
    cliPrintf("reboot to BOOTSEL...\n");
    delay(100);
    usbRebootBootsel();
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("usb info\n");
    cliPrintf("usb boot\n");
  }
}
#endif

#endif
