#include "usbh.h"

#ifdef _USE_HW_USBH
#include "cli.h"
#include "log.h"
#include "tusb.h"
#include "pio_usb.h"
#include "pio_usb_ll.h"
#include "host/hcd.h"
#include "pico/multicore.h"
#include "pico/time.h"


// PIO USB 는 타이밍에 민감해서 전용 코어를 쓴다.
//   core0 : tud_task() + CLI + LED
//   core1 : tuh_task() 만
//
// PIO 자원 (PIO_USB_DEFAULT_CONFIG)
//   pio0 sm0 : TX
//   pio0 sm1 : RX
//   pio0 sm2 : EOP
//   DMA ch0  : TX
// pio0 를 통째로 쓰므로 WS2812 는 pio1 로 옮겼다 (hw_def.h).


#ifdef _USE_HW_CLI
static void cliCmd(cli_args_t *args);
#endif

static void usbhCore1Main(void);


static volatile bool     is_running  = false;
static volatile uint32_t task_count  = 0;
static volatile bool     cfg_ret     = false;
static volatile bool     init_ret    = false;




bool usbhInit(void)
{
  usbhHidInit();

  multicore_reset_core1();
  multicore_launch_core1(usbhCore1Main);

  // core1 이 PIO / DMA / 알람풀을 잡을 때까지 기다린다.
  // 여기서 안 기다리면 core0 이 먼저 다른 자원을 건드릴 수 있다.
  for (int i=0; i<100; i++)
  {
    if (is_running == true) break;
    delay(10);
  }

  logPrintf("USB Host \t\t: %s\r\n", is_running ? "core1 running" : "FAIL");

#ifdef _USE_HW_CLI
  cliAdd("usbh", cliCmd);
#endif

  return is_running;
}

bool usbhIsRunning(void)
{
  return is_running;
}

uint32_t usbhGetTaskCount(void)
{
  return task_count;
}


void usbhCore1Main(void)
{
  pio_usb_configuration_t config = PIO_USB_DEFAULT_CONFIG;

  config.pin_dp = HW_USBH_DP_PIN;
  config.pinout = HW_USBH_PINOUT;

  // SOF 인터럽트를 core1 에서 돌리려면 알람풀도 core1 에서 만들어야 한다.
  // 하드웨어 알람 2 번은 SDK 기본 풀(3번)과 겹치지 않는다.
  config.alarm_pool = (void *)alarm_pool_create(HW_USBH_ALARM_NUM, 1);

  cfg_ret  = tuh_configure(HW_USBH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &config);
  init_ret = tuh_init(HW_USBH_RHPORT);

  is_running = true;

  while(1)
  {
    tuh_task();
    task_count++;
  }
}


#ifdef _USE_HW_CLI
void cliCmd(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    usbh_hid_info_t info;

    cliPrintf("core1     : %s\n", usbhIsRunning() ? "running" : "stopped");
    cliPrintf("task cnt  : %d\n", usbhGetTaskCount());
    cliPrintf("D+ / D-   : GPIO%d / GPIO%d\n", HW_USBH_DP_PIN,
              (HW_USBH_PINOUT == PIO_USB_PINOUT_DPDM) ? HW_USBH_DP_PIN+1 : HW_USBH_DP_PIN-1);
    cliPrintf("configure : %d\n", cfg_ret);
    cliPrintf("tuh_init  : %d\n", init_ret);
    cliPrintf("connect st: %d\n", hcd_port_connect_status(HW_USBH_RHPORT));
    cliPrintf("speed     : %s\n",
              hcd_port_speed_get(HW_USBH_RHPORT) == TUSB_SPEED_FULL ? "full" : "low");
    cliPrintf("frame num : %d\n", hcd_frame_number(HW_USBH_RHPORT));
    cliPrintf("mounted   : %d\n", tuh_mounted(1));
    cliPrintf("connected : %d\n", usbhHidIsConnected());
    cliPrintf("rx / drop : %d / %d\n", usbhHidGetRxCount(), usbhHidGetDropCount());

    for (int i=0; i<CFG_TUH_HID; i++)
    {
      if (usbhHidGetInfo(i, &info) && info.is_connect)
      {
        const char *p_str = "none";
        if (info.itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) p_str = "keyboard";
        if (info.itf_protocol == HID_ITF_PROTOCOL_MOUSE)    p_str = "mouse";

        cliPrintf("  [%d] addr %d  %04X:%04X  %s\n",
                  i, info.dev_addr, info.vid, info.pid, p_str);
      }
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "dump"))
  {
    usbh_hid_report_t report;

    cliPrintf("아무 키나 누르면 멈춘다\n");

    while(cliKeepLoop())
    {
      if (usbhHidGetReport(&report) != true)
      {
        // delay() 안에서 cliLoopIdle() 이 돌아 USB 가 계속 살아 있다.
        delay(1);
        continue;
      }

      cliPrintf("i%d p%d len%2d : ", report.instance, report.protocol, report.len);
      for (int i=0; i<report.len; i++)
      {
        cliPrintf("%02X ", report.data[i]);
      }
      cliPrintf("\n");
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("usbh info\n");
    cliPrintf("usbh dump\n");
  }
}
#endif

#endif
