#include "ap.h"
#include "led_status.h"
#include "usbd_hid.h"
#include "link.h"
#include "qmk/qmk.h"


#ifdef _USE_HW_USBH
static bool isKeyDown(const usbh_hid_report_t *p_report);
static void updateKeyboard(void);
#ifdef _USE_HW_CLI
static void cliKey(cli_args_t *args);
#endif
#endif




void apInit(void)
{
  cliOpen(HW_UART_CH_CLI, 115200);

  ledStatusInit();
  qmkCliInit();

#if defined(_USE_HW_CLI) && defined(_USE_HW_USBH)
  cliAdd("key", cliKey);
#endif

  // ★ 06단계부터 부팅 때 자동으로 올린다.
  //
  //   05단계까지는 `qmk start` 로만 켰다 — 이식 중에 qmkInit() 안에서 죽으면
  //   USB 가 통째로 안 올라와 BOOTSEL 로만 되살릴 수 있어서였다.
  //   이제 VIA 까지 실기에서 확인됐고, 자동으로 안 켜면 재부팅할 때마다
  //   CLI 를 붙이기 전에는 키보드가 아예 동작하지 않는다.
  //
  //   그래도 되살릴 길은 남아 있다 — Key2(Reset) 더블클릭이면 BOOTSEL 이다.
  qmkStart();
}

void apMain(void)
{
  while(1)
  {
#ifdef _USE_HW_USBH
    updateKeyboard();
#endif

    qmkUpdate();
    ledStatusUpdate();

    usbUpdate();
    cliMain();
  }
}


#ifdef _USE_HW_USBH

// USB-A 에서 올라온 리포트를 꺼내 PC 로 그대로 넘긴다.
//
// 05단계에서는 ap/modules/link 가 가상 매트릭스로 바꿔 QMK 에 넣는다.
// 지금은 QMK 없이 "USB 연장선" 이다.
//
// 이 루프가 없으면 core1 이 채운 큐가 가득 차서 계속 버려진다.
// 진단용 — 큐에서 꺼낸 리포트가 어디로 갔는지 센다.
// 리포트는 들어오는데(usbh rx) 키가 안 먹는 상황을 가리기 위한 것이다.
static uint32_t kbd_drain_cnt = 0;   // 큐에서 꺼낸 총 개수
static uint32_t kbd_pass_cnt  = 0;   // keyboard 프로토콜 + len>=8 (link 로 간 것)
static uint32_t kbd_drop_cnt  = 0;   // 그 외 — 조용히 버려진 것
static usbh_hid_report_t kbd_last_ok;
static usbh_hid_report_t kbd_last_drop;

void updateKeyboard(void)
{
  usbh_hid_report_t report;

  while(usbhHidGetReport(&report) == true)
  {
    kbd_drain_cnt++;

    if (report.protocol == HID_ITF_PROTOCOL_KEYBOARD && report.len >= 8)
    {
      kbd_pass_cnt++;
      kbd_last_ok = report;
    }
    else
    {
      kbd_drop_cnt++;
      kbd_last_drop = report;
    }

    switch(report.protocol)
    {
      case HID_ITF_PROTOCOL_KEYBOARD:
        if (report.len >= 8)
        {
          if (qmkIsOn() == true && qmkIsPassthrough() == false)
          {
            // QMK 가 올라와 있으면 비트맵만 채운다.
            // port/matrix.c 가 읽어가고 QMK 가 키맵을 태워 내보낸다.
            linkSetKeyboardReport(report.data, report.len);
          }
          else
          {
            // 패스스루면 QMK 를 거치지 않는다 (VIA 의 Link > Passthrough).
            // 키맵이 꼬였을 때의 탈출구다.
            // QMK 가 없으면 04단계처럼 그대로 흘린다.
            // QMK 기동에 실패해도 키보드는 계속 쓸 수 있다.
            usbdHidSendKeyboard(report.data);
          }
        }
        if (isKeyDown(&report) == true)
        {
          ledStatusKeyEvent();
        }
        break;

      case HID_ITF_PROTOCOL_MOUSE:
        // boot mouse 리포트 : [0] 버튼 [1] X [2] Y [3] 휠
        if (report.len >= 3)
        {
          usbdHidSendMouse(report.data[0],
                           (int8_t)report.data[1],
                           (int8_t)report.data[2],
                           (report.len >= 4) ? (int8_t)report.data[3] : 0,
                           0);
        }
        break;

      default:
        // report protocol 장치는 05단계에서 디스크립터를 파싱해 다룬다.
        break;
    }
  }
}

// boot keyboard 리포트에서 "새로 눌린 키" 가 있는지 본다.
//   data[0]    모디파이어 비트
//   data[2..7] 눌려 있는 키코드 (순서는 보장되지 않는다)
//
// 키를 떼거나 아무 변화가 없는 리포트는 무시한다.
// 이 키보드는 폴링마다 리포트를 올려서 그냥 세면 계속 반짝인다.
bool isKeyDown(const usbh_hid_report_t *p_report)
{
  static uint8_t pre_data[8] = {0, };
  bool is_down = false;

  if (p_report->protocol != HID_ITF_PROTOCOL_KEYBOARD) return false;
  if (p_report->len < 8) return false;

  // 직전에 안 눌려 있던 모디파이어가 눌렸나
  if ((p_report->data[0] & ~pre_data[0]) != 0)
  {
    is_down = true;
  }

  for (int i=2; i<8 && is_down == false; i++)
  {
    uint8_t keycode = p_report->data[i];
    bool    is_pre  = false;

    // 0 = 빈 자리, 1 = ErrorRollOver (6키 초과). 둘 다 키가 아니다.
    if (keycode == 0 || keycode == 1) continue;

    for (int j=2; j<8; j++)
    {
      if (pre_data[j] == keycode)
      {
        is_pre = true;
        break;
      }
    }

    if (is_pre == false) is_down = true;
  }

  memcpy(pre_data, p_report->data, 8);

  return is_down;
}

#endif


#if defined(_USE_HW_CLI) && defined(_USE_HW_USBH)
static void dumpReport(const char *p_name, const usbh_hid_report_t *p_report)
{
  cliPrintf("%s : inst %d  proto %d  len %d  ",
            p_name, p_report->instance, p_report->protocol, p_report->len);
  for (int i=0; i<p_report->len && i<8; i++) cliPrintf("%02X ", p_report->data[i]);
  cliPrintf("\n");
}

static void cliKey(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 0 || (args->argc == 1 && args->isStr(0, "info")))
  {
    cliPrintf("usbh rx/drop : %d / %d\n",
              usbhHidGetRxCount(), usbhHidGetDropCount());
    cliPrintf("drain        : %d   (큐에서 꺼낸 것)\n", kbd_drain_cnt);
    cliPrintf("  -> link    : %d   (keyboard proto + len>=8)\n", kbd_pass_cnt);
    cliPrintf("  -> 버림    : %d   (그 외 — 조용히 사라진다)\n", kbd_drop_cnt);
    cliPrintf("qmk          : %s%s\n",
              qmkIsOn() ? "on" : "off",
              qmkIsPassthrough() ? " (passthrough)" : "");
    cliPrintf("link set     : %d\n", linkGetSetCount());

    dumpReport("last ok  ", &kbd_last_ok);
    dumpReport("last 버림", &kbd_last_drop);

    cliPrintf("HID 인스턴스\n");
    for (int i=0; i<CFG_TUH_HID; i++)
    {
      usbh_hid_info_t info;

      if (usbhHidGetInfo(i, &info) != true) continue;
      cliPrintf("  [%d] connect %d  addr %d  proto %d  %04X:%04X\n",
                i, info.is_connect, info.dev_addr, info.itf_protocol,
                info.vid, info.pid);
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "watch"))
  {
    uint32_t pre_drain = 0;
    uint32_t pre_pass  = 0;

    cliPrintf("초당 변화량. 아무 키나 누르면 멈춘다\n");
    while(cliKeepLoop())
    {
      cliPrintf("drain +%-5d  link +%-5d  버림 +%-5d\n",
                kbd_drain_cnt - pre_drain,
                kbd_pass_cnt - pre_pass,
                kbd_drop_cnt);
      pre_drain = kbd_drain_cnt;
      pre_pass  = kbd_pass_cnt;
      delay(1000);
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("key info\n");
    cliPrintf("key watch\n");
  }
}
#endif


// bsp 의 delay() 와 cliKeepLoop() 이 이걸 부른다.
//
// ★ 계속 돌아야 하는 것은 전부 여기에 둔다.
//
//   오래 도는 CLI 명령(qmk matrix, usbh dump ...) 안에서는 apMain() 의 루프가
//   진행되지 않는다. 여기에 안 넣으면 그 동안 USB 가 끊기거나 키 입력이 멎는다.
//   실제로 usbUpdate() 만 넣어 뒀다가 qmk matrix 가 아무것도 못 보여줬다.
void cliLoopIdle(void)
{
  usbUpdate();

#ifdef _USE_HW_USBH
  updateKeyboard();
#endif

  qmkUpdate();
}
