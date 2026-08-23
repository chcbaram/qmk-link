#include "usbh.h"

#ifdef _USE_HW_USBH
#include "cli.h"
#include "log.h"
#include "tusb.h"
#include "pio_usb.h"
#include "pio_usb_ll.h"
#include "host/hcd.h"
#include "pico/multicore.h"
#include "pico/flash.h"
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

/*
 * ★ pio_usb_host_stop() / pio_usb_host_restart() 를 쓰지 않는다.
 *
 *   Pico-PIO-USB 0.7.2 에서 이 둘은 죽은 코드다. 플래그를 세우고 그것이
 *   내려가기를 무한 대기하는데, 그 플래그를 내리는 곳이 소스 어디에도 없다.
 *
 *     pio_usb_host.c:102   cancel_timer_flag = true;
 *     pio_usb_host.c:103   while (cancel_timer_flag) { continue; }
 *
 *   부르는 순간 그 코어가 멈춘다. 실제로 겪었다.
 *
 * ★ hcd_event_device_remove/attach 로 재열거시키는 것도 안 통했다.
 *
 *   떼기는 되는데 다시 붙지 못한다. 정지를 뒤처리로 덮으려 하지 말고
 *   애초에 만들지 않는다 — copy_to_ram (src/CMakeLists.txt).
 */


void usbhCore1Main(void)
{
  pio_usb_configuration_t config = PIO_USB_DEFAULT_CONFIG;

  // ★ core0 이 플래시를 쓰는 동안 이 코어를 세울 수 있게 등록한다.
  //
  //   소거·기록 중에는 XIP 가 멈춘다. 이걸 부르지 않으면 core0 의
  //   flash_safe_execute() 가 "저쪽이 뭘 하는지 모른다" 며 거절한다.
  //   등록해 두면 SDK 가 이 코어를 RAM 안 루프에 세웠다가 끝나면 풀어 준다.
  //   그동안 tuh_task() 가 멈추므로 그 시간을 짧게 유지하는 게 중요하다
  //   (`flash test` 로 잰다).
  flash_safe_execute_core_init();

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

    // 이름 요청 재시도. 컨트롤 전송이라 열거 중에는 거절당한다 (usbh_hid.c 주석)
    usbhHidTask();

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
