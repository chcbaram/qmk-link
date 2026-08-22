#include "ap.h"
#include "led_status.h"
#include "usbd_hid.h"


#ifdef _USE_HW_USBH
static bool isKeyDown(const usbh_hid_report_t *p_report);
static void updateKeyboard(void);
#endif




void apInit(void)
{
  cliOpen(HW_UART_CH_CLI, 115200);

  ledStatusInit();
}

void apMain(void)
{
  while(1)
  {
#ifdef _USE_HW_USBH
    updateKeyboard();
#endif

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
void updateKeyboard(void)
{
  usbh_hid_report_t report;

  while(usbhHidGetReport(&report) == true)
  {
    switch(report.protocol)
    {
      case HID_ITF_PROTOCOL_KEYBOARD:
        if (report.len >= 8)
        {
          usbdHidSendKeyboard(report.data);
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


// cli 가 오래 걸리는 명령을 도는 동안에도 USB 는 살아 있어야 한다.
void cliLoopIdle(void)
{
  usbUpdate();
}
