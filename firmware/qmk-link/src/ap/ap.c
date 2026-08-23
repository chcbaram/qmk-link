#include "ap.h"
#include "led_status.h"
#include "usbd_hid.h"
#include "link.h"
#include "kbd_store.h"
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
  kbdStoreInit();
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
    // ★ 계속 돌아야 하는 것은 cliLoopIdle() 안에만 둔다.
    //
    //   예전에는 여기에 같은 목록을 한 벌 더 적어 뒀는데, 한쪽에만 추가하는
    //   실수가 났다 — usbdHidUpdate() 를 cliLoopIdle() 에만 넣는 바람에
    //   평상시에는 재시도가 아예 안 돌았고, 못 보낸 뗌 리포트가 보류함에
    //   갇혀 PC 쪽에 키가 눌린 채로 남았다.
    //
    //   목록을 한 군데로 모으면 그 실수가 구조적으로 불가능해진다.
    cliLoopIdle();

    ledStatusUpdate();
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

  // ★ 키보드가 빠지면 그 키보드가 누르고 있던 키를 뗀다.
  //
  //   안 그러면 그 순간 눌려 있던 키가 매트릭스에 남고, 아무도 떼 주지 않는다.
  //   QMK 는 계속 눌린 것으로 보고 PC 에 키를 물고 있는다 — 타이핑이 죽은 것처럼
  //   보인다.
  //
  //   ★ 인스턴스별로 본다. 두 대가 붙어 있을 때 한 대만 빼면 다른 대의 키까지
  //     떼면 안 된다.
  {
    static bool connect_pre[CFG_TUH_HID] = {false, };
    bool        is_changed = false;

    for (int i=0; i<CFG_TUH_HID; i++)
    {
      usbh_hid_info_t info;

      if (usbhHidGetInfo(i, &info) != true) continue;

      if (info.is_connect != connect_pre[i])
      {
        connect_pre[i] = info.is_connect;
        if (info.is_connect == false) linkClearInstance(i);

        is_changed = true;
      }
    }

    // ★ 꽂힌 키보드가 바뀌면 그 키보드의 레이아웃 칸을 고른다.
    //
    //   Vial 정의 서빙(port/vial_port.c)이 이 값을 본다.
    //   저장된 것이 없으면 -1 이 되고, 그러면 컴파일에 박힌 기본 배열을 쓴다.
    //
    //   키보드 인터페이스를 고른다 — 같은 장치의 컨슈머 인터페이스도 같은
    //   vid/pid 라 어느 쪽을 잡아도 값은 같지만, 없을 때 0 을 넘겨야 한다.
    if (is_changed == true)
    {
      usbh_hid_info_t info;
      uint16_t        vid = 0;
      uint16_t        pid = 0;

      for (int i=0; i<CFG_TUH_HID; i++)
      {
        if (usbhHidGetInfo(i, &info) != true) continue;
        if (info.is_connect != true) continue;

        vid = info.vid;
        pid = info.pid;
        if (info.itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) break;
      }

      kbdStoreSelect(vid, pid, 0);
    }
  }

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
            linkSetKeyboardReport(report.instance, report.data, report.len);
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

      case HID_ITF_PROTOCOL_NONE:
        /*
         * ★ 미디어키 (볼륨 · 재생 · 뮤트).
         *
         *   키보드 인터페이스가 아니라 Consumer 페이지(0x0C)를 쓰는 별도
         *   인터페이스로 온다. mount 때 디스크립터를 파싱해 표시해 뒀다
         *   (usbh_hid.c 의 is_consumer).
         *
         *   ★ QMK 를 거치지 않고 그대로 흘린다.
         *
         *     QMK 의 키맵은 매트릭스 좌표 기반인데 컨슈머 usage 는 거기 없다.
         *     마우스와 같은 취급이다.
         *
         *   보통 형식은 usage 16비트 하나다 (0 = 뗌).
         *   report_id 가 있으면 첫 바이트가 ID 라 건너뛴다.
         */
        if (report.is_consumer == true)
        {
          const uint8_t *p_val = report.data;
          uint8_t        len   = report.len;

          if (report.report_id != 0 && len > 0) { p_val++; len--; }

          if (len >= 2)
          {
            usbdHidSendConsumer((uint16_t)p_val[0] | ((uint16_t)p_val[1] << 8));
          }
          else if (len == 1)
          {
            usbdHidSendConsumer((uint16_t)p_val[0]);
          }
        }
        break;

      default:
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

    {
      usbd_hid_kbd_stat_t st;

      usbdHidGetKbdStat(&st);
      cliPrintf("PC 로 보내기 (IF0 키보드)\n");
      cliPrintf("  호출 %d  보냄 %d  같아서 건너뜀 %d\n",
                st.try_cnt, st.sent_cnt, st.same_cnt);
      cliPrintf("  not ready %d   전송실패 %d   나중에보냄 %d   보류 %d\n",
                st.busy_cnt, st.fail_cnt, st.retry_cnt, st.pending);
      cliPrintf("  ready %d  mounted %d  suspend %d\n",
                st.is_ready, st.is_mount, st.is_susp);
    }

    dumpReport("last ok  ", &kbd_last_ok);
    dumpReport("last 버림", &kbd_last_drop);

    cliPrintf("HID 인스턴스\n");
    for (int i=0; i<CFG_TUH_HID; i++)
    {
      usbh_hid_info_t info;

      if (usbhHidGetInfo(i, &info) != true) continue;
      cliPrintf("  [%d] connect %d  addr %d  proto %d  %04X:%04X  %s\n",
                i, info.is_connect, info.dev_addr, info.itf_protocol,
                info.vid, info.pid,
                info.is_consumer ? "consumer(미디어키)" : "");
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

#ifdef _USE_HW_USB
  // 엔드포인트가 바빠서 못 보낸 키 리포트를 마저 보낸다.
  usbdHidUpdate();
#endif

#ifdef _USE_HW_USBH
  updateKeyboard();
#endif

  qmkUpdate();
}
