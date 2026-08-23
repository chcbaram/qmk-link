/*
 * ap.c 는 **진입점만** 갖는다.
 *
 *   apInit()      올릴 것을 올린다
 *   apMain()      메인 루프
 *   cliLoopIdle() 계속 돌아야 하는 것들 (아래 ★ 주석)
 *
 * 실제 일은 ap/modules/ 아래에 있다. USB-A 키보드를 받아 넘기는 300줄은
 * link/link_kbd.c 로 뺐다.
 */
#include "ap.h"
#include "led_status.h"
#include "usbd_hid.h"
#include "link_kbd.h"
#include "kbd_store.h"
#include "qmk/qmk.h"




void apInit(void)
{
  cliOpen(HW_UART_CH_CLI, 115200);

  ledStatusInit();
  kbdStoreInit();
  linkKbdInit();
  qmkCliInit();

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
  // USB-A 리포트를 꺼내 QMK / PC 로 보낸다 (link/link_kbd.c)
  linkKbdUpdate();
#endif

  qmkUpdate();

#ifdef _USE_HW_USB
  // ★ qmkUpdate() **다음**이다.
  //
  //   레이아웃을 담는 우리 raw HID 명령이 거기서 처리되고, 담자마자
  //   kbdStoreReselect() 로 칸이 바뀐다. 응답은 이미 나간 뒤라 여기서 끊어도
  //   업로드 도구가 답을 놓치지 않는다.
  linkKbdApplySlot();
#endif
}
