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
//
// ★ 왜 바로 안 끊고 뜸을 들이나
//
//   PID 가 바뀌는 계기 중 하나가 "웹/도구가 레이아웃을 담았다" 이고, 그 COMMIT
//   응답은 아직 **엔드포인트에 실려만 있다.** usbdHidSendRaw() 는 큐에 넣고
//   바로 돌아온다 — 실제 전송은 호스트가 다음 IN 토큰을 보낼 때다.
//   그 자리에서 끊으면 도구가 답을 영영 못 받는다.
//
//   그래서 요청을 받으면 ARM 만 걸고, 조용해진 뒤에 끊는다.
//   연달아 바뀌어도 마지막 값 하나로 합쳐지는 부수 효과도 있다.
//
// ★ 그리고 우리 도구가 말을 걸고 있는 동안은 계속 뒤로 민다.
//
//   웹 마법사는 20ms 마다 우리 명령을 보낸다. 그동안 끊으면 담고 · 고치고 ·
//   적용하는 내내 연결이 떨어져서 쓸 수가 없다. linkCmdHandle() 이 명령마다
//   usbPostponeReenum() 을 불러 예약을 되감으므로, 실제로 끊기는 시점은
//   **사용자가 마법사를 끝낼 때** 하나로 모인다. 그때 VIA/Vial 로 가니 딱 맞다.
//   VIA/Vial 트래픽은 우리 명령이 아니라서 밀지 않는다.
//
// ★ 부팅 때도 붙는 것을 잠깐 미룬다.
//
//   키보드를 꽂은 채로 켜면 아직 어느 SLOT 인지 모르는 채로 0x5305 로 떴다가
//   곧 0x54NN 으로 다시 떴다 — 열거가 두 번이었다. 키보드가 인식될 때까지
//   (최대 USB_BOOT_HOLD_MS) 붙이지 않으면 처음부터 맞는 PID 로 한 번만 뜬다.
#define USB_REENUM_ARM_MS   250     /* 요청 -> 실제로 끊기까지 */
#define USB_REENUM_GAP_MS   100     /* 끊고 -> 다시 붙이기까지 */
#define USB_BOOT_HOLD_MS    1500    /* 부팅 후 PID 가 정해지기를 기다리는 한계 */

enum
{
  REENUM_IDLE = 0,
  REENUM_ARMED,
  REENUM_GAP,
};

static uint8_t  reenum_state = REENUM_IDLE;
static uint32_t reenum_time  = 0;
static uint16_t reenum_pid   = 0;
static uint32_t reenum_count = 0;

static bool     boot_hold    = false;
static uint32_t boot_time    = 0;


#ifdef _USE_HW_CLI
static void cliCmd(cli_args_t *args);
#endif




bool usbInit(void)
{
  usbdDescInit();
  usbdHidInit();

  tud_init(BOARD_TUD_RHPORT);

  /* ★ 아직 붙이지 않는다. PID 가 정해지면 그때 붙는다 (위 주석 참고) */
  tud_disconnect();
  boot_hold = true;
  boot_time = millis();

  is_init = true;

#ifdef _USE_HW_CLI
  cliAdd("usb", cliCmd);
#endif

  return true;
}

void usbUpdate(void)
{
  if (is_init != true) return;

  // 부팅 직후 — PID 가 정해지거나 한계 시간이 지나면 붙는다
  if (boot_hold == true)
  {
    if ((millis() - boot_time) >= USB_BOOT_HOLD_MS)
    {
      boot_hold = false;
      tud_connect();
      logPrintf("[  ] usb 부착 (PID %04X, 대기 만료)\r\n", usbdDescGetProductId());
    }

    tud_task();
    return;
  }

  switch(reenum_state)
  {
    case REENUM_ARMED:
      if ((millis() - reenum_time) < USB_REENUM_ARM_MS) break;

      // 여기서야 descriptor 를 바꾼다. 그 전에 호스트가 물어보면 옛 값이 맞다.
      usbdDescSetProductId(reenum_pid);
      logPrintf("[  ] usb PID -> %04X (재열거)\r\n", reenum_pid);
      tud_disconnect();
      reenum_time  = millis();
      reenum_state = REENUM_GAP;
      reenum_count++;
      break;

    case REENUM_GAP:
      if ((millis() - reenum_time) < USB_REENUM_GAP_MS) break;

      tud_connect();
      reenum_state = REENUM_IDLE;
      break;

    default:
      break;
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

  /*
   * ★ 아직 안 붙었으면 끊을 것도 없다 — 값만 넣고 바로 붙인다.
   *
   *   부팅 때 PID 가 정해지는 순간이 여기다. 값이 그대로면(담아 둔 SLOT 이
   *   없다) 기다림을 그대로 두고 한계 시간에 붙는다 — 부팅 직후에는 아직
   *   키보드가 안 올라와서 "그대로" 인 것뿐이라 여기서 붙이면 안 된다.
   */
  if (boot_hold == true)
  {
    if (usbdDescGetProductId() == pid) return;

    usbdDescSetProductId(pid);
    boot_hold = false;
    tud_connect();
    logPrintf("[  ] usb 부착 (PID %04X)\r\n", pid);
    return;
  }

  // 이미 그 값이고 예약된 것도 없으면 할 일이 없다.
  if (reenum_state == REENUM_IDLE && usbdDescGetProductId() == pid) return;

  reenum_pid   = pid;
  reenum_time  = millis();
  reenum_state = REENUM_ARMED;      /* 예약만 한다. 끊는 것은 usbUpdate() 가 */
}

void usbPostponeReenum(void)
{
  /* 예약된 것만 민다. 이미 끊은 뒤(GAP)라면 되돌릴 수 없다 */
  if (reenum_state == REENUM_ARMED) reenum_time = millis();
}

bool usbIsReenumPending(void)
{
  return (reenum_state != REENUM_IDLE);
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
    cliPrintf("PID       : %04X  (재열거 %d 회)%s%s\n",
              usbGetProductId(), (int)reenum_count,
              reenum_state == REENUM_ARMED ? "  ★ 예약됨" : "",
              boot_hold ? "  ★ 부착 대기" : "");
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
