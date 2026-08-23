/*
 * qmk.c — QMK 코어 구동
 *
 * 이 보드에서 QMK 가 맡는 일은 매트릭스 위쪽뿐이다 — 키맵, 레이어, 탭홀드, 매크로.
 * 아래쪽은 USB 호스트다. USB-A 에 꽂힌 키보드가 보낸 HID 리포트를
 * ap/modules/link 가 비트맵으로 바꿔 두고 port/matrix.c 가 그걸 읽는다.
 *
 * ★ 부팅 때 자동으로 켜지 않는다.
 *
 *   qmkInit() 안에서 죽으면 USB 가 통째로 안 올라와서 BOOTSEL 로만 되살릴 수 있다.
 *   이식 중에는 CLI `qmk start` 로만 켠다. 동작이 확인되면 부팅 때 켜도록 바꾼다.
 *   (wish-he 가 같은 이유로 그렇게 했다)
 */

#include "qmk.h"
#include "link.h"
#include "host.h"
#include "eeprom.h"
#include "eeconfig.h"
#ifdef RAW_ENABLE
#include "raw_hid.h"
#endif
#include "flash.h"
#include "keyboard.h"
#include "matrix.h"
#include "action.h"
#include "action_util.h"
#include "keycode_config.h"
#include "usb_device_state.h"
#include "usbd_hid.h"
#include <string.h>


extern host_driver_t usb_driver;    /* port/driver_usb.c */

static void cliCmd(cli_args_t *args);

static bool     is_qmk_on      = false;
static bool     is_passthrough = false;
static uint32_t task_count  = 0;




static bool qmkInit(void)
{
  // 단계마다 로그를 남긴다. 어딘가에서 죽으면 마지막 줄이 범인이다.
  logPrintf("[  ] qmk 1 eeprom_driver_init\r\n");
  eeprom_driver_init();

  logPrintf("[  ] qmk 2 host_set_driver\r\n");
  host_set_driver(&usb_driver);

  logPrintf("[  ] qmk 3 keyboard_setup\r\n");
  keyboard_setup();

  logPrintf("[  ] qmk 4 keyboard_init\r\n");
  keyboard_init();

  logPrintf("[OK] qmkInit()\r\n");
  logPrintf("     MATRIX %d x %d, 디바운스 없음\r\n", MATRIX_ROWS, MATRIX_COLS);

  return true;
}

bool qmkCliInit(void)
{
  cliAdd("qmk", cliCmd);
  return true;
}

bool qmkStart(void)
{
  if (is_qmk_on) return true;

  is_qmk_on = qmkInit();
  return is_qmk_on;
}

bool qmkIsOn(void)
{
  return is_qmk_on;
}

void qmkSetPassthrough(bool enable)
{
  if (is_passthrough == enable) return;

  is_passthrough = enable;

  // 바꾸는 순간 눌려 있던 키는 QMK 쪽에 남는다. 아무도 안 뗀다.
  clear_keyboard();
}

bool qmkIsPassthrough(void)
{
  return is_passthrough;
}

void qmkUpdate(void)
{
  // ★ QMK 가 꺼져 있어도 돌린다.
  //
  //   `qmk start` 로 켰다가 끈 뒤에도 미저장 dirty 섹터가 남아 있을 수 있다.
  //   여기서 빠지면 그대로 날아간다.
  eeprom_task();

  if (is_qmk_on != true) return;

#ifdef RAW_ENABLE
  // ★ VIA 요청 처리는 여기서 한다 — USB 콜백 안이 아니다 (usbd_hid.h 주석 참고).
  {
    uint8_t raw_data[HID_RAW_REPORT_LEN];

    while (usbdHidGetRaw(raw_data) == true)
    {
      raw_hid_receive(raw_data, HID_RAW_REPORT_LEN);
    }
  }
#endif

  // ★ 호스트가 HID 프로토콜을 바꾸면 눌린 키를 비운다.
  //
  //   boot <-> report 로 갈리면 리포트가 나가는 인터페이스가 통째로 바뀐다
  //   (IF0 6KRO <-> IF1 NKRO). 그때 눌려 있던 키는 옛 인터페이스에 남은 채로
  //   아무도 안 뗀다.
  {
    static uint8_t proto_pre = 1;
    uint8_t        proto     = usbdHidGetProtocol();

    if (proto != proto_pre)
    {
      proto_pre = proto;
      clear_keyboard();
      logPrintf("[  ] HID protocol %s\r\n", proto ? "report" : "boot");
    }
  }

  keyboard_task();
  task_count++;
}


static void cliCmd(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "start"))
  {
    cliPrintf("qmkInit() ...\n");
    cliPrintf("%s\n", qmkStart() ? "OK" : "fail");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("qmk       : %s\n", is_qmk_on ? "on" : "off");
    cliPrintf("MATRIX    : %d x %d\n", MATRIX_ROWS, MATRIX_COLS);
    cliPrintf("task cnt  : %d\n", task_count);
    cliPrintf("protocol  : %d (%s)\n", usbdHidGetProtocol(),
              usbdHidGetProtocol() ? "report - NKRO" : "boot - 6KRO");
    cliPrintf("nkro      : %d\n", keymap_config.nkro);
    cliPrintf("link set  : %d 회, 지금 눌린 키 %d\n",
              linkGetSetCount(), linkGetKeyCount());
    cliPrintf("link rows : ");
    for (uint8_t r=0; r<LINK_MATRIX_ROWS; r++)
      if (linkGetRow(r)) cliPrintf("[%d]=%04X ", r, linkGetRow(r));
    cliPrintf("\n");
    cliPrintf("mtx  rows : ");
    for (uint8_t r=0; r<MATRIX_ROWS; r++)
      if (matrix_get_row(r)) cliPrintf("[%d]=%04X ", r, (unsigned)matrix_get_row(r));
    cliPrintf("\n");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "matrix"))
  {
    cliPrintf("눌린 키를 보여준다 — 아무 키나 누르면 멈춘다\n");

    while(cliKeepLoop())
    {
      cliPrintf("\r");
      for (uint8_t r=0; r<MATRIX_ROWS; r++)
      {
        matrix_row_t bits = matrix_get_row(r);

        for (uint8_t c=0; c<MATRIX_COLS; c++)
        {
          if (bits & ((matrix_row_t)1<<c))
          {
            cliPrintf("0x%02X ", (r<<4) | c);
          }
        }
      }
      cliPrintf("      ");
      delay(50);
    }
    cliPrintf("\n");
    ret = true;
  }

  /*
   * EEPROM 은 RAM 섀도 + 지연 플러시다 (port/platforms/eeprom.c).
   * 여기서 섀도와 플래시를 나란히 보여 준다 — 둘이 같아졌으면 저장이 끝난 것이다.
   */
  if (args->argc >= 1 && args->isStr(0, "eeprom"))
  {
    uint8_t  shadow[32];
    uint8_t  onflash[32];

    if (args->argc == 2 && args->isStr(1, "flush"))
    {
      uint32_t exe_time = micros();
      eeprom_flush();
      cliPrintf("eeprom_flush() : %d us\n", (int)(micros() - exe_time));
    }

    if (args->argc == 2 && args->isStr(1, "erase"))
    {
      eeprom_driver_erase();
      eeprom_flush();
      cliPrintf("eeprom 전체 소거\n");
    }

    cliPrintf("size      : %d B  @ 0x%06X\n",
              TOTAL_EEPROM_BYTE_COUNT, (unsigned)eepromGetBase());
    cliPrintf("섀도      : %s\n",
              eepromIsInit() ? "읽어 둠" : "미초기화 (qmk start 전)");
    cliPrintf("dirty     : 0x%X\n", (unsigned)eepromGetDirtyMask());
    cliPrintf("flush cnt : %d 회\n", (int)eepromGetFlushCount());
    cliPrintf("flush time: %d us (마지막 섹터)\n", (int)eepromGetFlushTime());
    cliPrintf("eeconfig  : %s\n", eeconfig_is_enabled() ? "enabled" : "disabled");

    eeprom_read_block(shadow, (const void *)0, sizeof(shadow));
    flashRead(eepromGetBase(), onflash, sizeof(onflash));

    cliPrintf("shadow    : ");
    for (int i=0; i<32; i++) cliPrintf("%02X ", shadow[i]);
    cliPrintf("\n");
    cliPrintf("flash     : ");
    for (int i=0; i<32; i++) cliPrintf("%02X ", onflash[i]);
    cliPrintf("\n");
    if (eepromIsInit() == true)
    {
      cliPrintf("일치      : %s\n",
                memcmp(shadow, onflash, sizeof(shadow)) == 0 ? "예" : "아니오 (아직 미저장)");
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("qmk start\n");
    cliPrintf("qmk info\n");
    cliPrintf("qmk matrix\n");
    cliPrintf("qmk eeprom [flush|erase]\n");
  }
}
