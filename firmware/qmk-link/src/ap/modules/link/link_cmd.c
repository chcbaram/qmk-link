#include "link_cmd.h"
#include "link.h"
#include "kbd_store.h"
#include "usbd_hid.h"
#include "usb.h"
#include "usbh.h"
#include "qmk/qmk.h"
#include <string.h>


/*
 * 이 명령 때문에 PC 쪽이 다시 열거될 것인가 — **언젠가** 그렇다는 뜻이다.
 *
 * ★ 곧바로 끊긴다는 뜻이 아니다 (버전 7 부터).
 *   우리 도구가 말을 거는 동안은 계속 미뤄지고, 조용해진 뒤에 끊긴다.
 *
 * ★ 응답을 보내는 지금은 아직 안 끊겼다. ap.c 의 updateProductId() 가
 *   cliLoopIdle() 끝에서 보고 결정한다. 그 판단을 여기서 미리 해 준다 —
 *   고른 SLOT 은 이미 바뀐 뒤고, 보고 중인 PID 는 아직 옛 값이다.
 */
static uint8_t willReenum(void)
{
#ifdef _USE_HW_USB
  int      slot = kbdStoreGetActive();
  uint16_t want = (slot >= 0) ? (uint16_t)(LINK_PID_BASE + slot) : (uint16_t)HW_USB_PID;

  return (want != usbGetProductId()) ? 1 : 0;
#else
  return 0;
#endif
}

bool linkCmdHandle(uint8_t *p_data, uint8_t length, bool vial_locked)
{
  if (length < 2) return false;
  if (p_data[0] != LINK_CMD_PREFIX) return false;

  switch (p_data[1])
  {
    case LINK_CMD_INFO:
    {
      uint8_t i = 2;
      uint8_t n = 0;

      p_data[i++] = LINK_CMD_VERSION;
#ifdef VIAL_ENABLE
      p_data[i++] = LINK_TREE_VIAL;
#else
      p_data[i++] = LINK_TREE_VIA;
#endif
      p_data[i++] = (vial_locked == true) ? 1 : 0;    /* 알림용 */
      p_data[i++] = LINK_MATRIX_ROWS;
      p_data[i++] = LINK_MATRIX_COLS;

      uint8_t *p_cnt = &p_data[i++];   /* 개수는 세고 나서 채운다 */

#ifdef _USE_HW_USBH
      for (int k=0; k<LINK_SOURCE_MAX; k++)
      {
        usbh_hid_info_t info;

        if (usbhHidGetInfo(k, &info) != true) continue;
        if (info.is_connect != true) continue;
        if (i + 4 > length) break;

        p_data[i++] = (uint8_t)(info.vid & 0xFF);
        p_data[i++] = (uint8_t)(info.vid >> 8);
        p_data[i++] = (uint8_t)(info.pid & 0xFF);
        p_data[i++] = (uint8_t)(info.pid >> 8);
        n++;
      }
#endif
      *p_cnt = n;

      memset(&p_data[i], 0, length - i);

      /* 뒤쪽 고정 자리 (버전 3) — memset 다음이어야 한다 */
      if (length >= 31)
      {
        int slot = kbdStoreGetActive();

        p_data[28] = (slot >= 0) ? (uint8_t)slot : 0xFF;
#ifdef _USE_HW_USB
        p_data[29] = (uint8_t)(usbGetProductId() & 0xFF);
        p_data[30] = (uint8_t)(usbGetProductId() >> 8);
#endif
        {
          int sel = kbdStoreGetSelected();

          p_data[31] = (sel >= 0) ? (uint8_t)sel : 0xFF;
        }
      }
      break;
    }

    case LINK_CMD_PRESSED:
    {
      uint8_t i = 3;
      uint8_t n = 0;

      /* ★ 잠금과 무관하게 답한다 (link_cmd.h 주석 참고) */
      {
        for (uint8_t r=0; r<LINK_MATRIX_ROWS; r++)
        {
          uint16_t bits = linkGetRow(r);

          if (bits == 0) continue;

          for (uint8_t c=0; c<LINK_MATRIX_COLS; c++)
          {
            if ((bits & (1U << c)) == 0) continue;
            if (i >= length) break;

            p_data[i++] = (uint8_t)((r << 4) | c);   /* 좌표가 곧 usage 다 */
            n++;
          }
        }
      }

      p_data[2] = n;
      memset(&p_data[i], 0, length - i);
      break;
    }

    case LINK_CMD_SLOT_INFO:
    {
      uint8_t   slot = p_data[2];
      kbd_hdr_t hdr;

      memset(&p_data[2], 0, length - 2);

      if (slot >= KBD_SLOT_MAX) { p_data[2] = LINK_RC_RANGE; break; }

      if (kbdStoreGetHeader(slot, &hdr) == true)
      {
        p_data[3] = 1;
        p_data[4] = (uint8_t)(hdr.vid & 0xFF);
        p_data[5] = (uint8_t)(hdr.vid >> 8);
        p_data[6] = (uint8_t)(hdr.pid & 0xFF);
        p_data[7] = (uint8_t)(hdr.pid >> 8);
        p_data[8] = (uint8_t)(hdr.data_len & 0xFF);
        p_data[9] = (uint8_t)(hdr.data_len >> 8);
        memcpy(&p_data[10], hdr.name, 21);
        p_data[31] = 0;
      }
      break;
    }

    case LINK_CMD_SLOT_READ:
    {
      uint8_t  slot   = p_data[2];
      uint16_t offset = (uint16_t)p_data[3] | ((uint16_t)p_data[4] << 8);
      uint8_t  n      = length - 4;      /* 28 */
      uint8_t  buf[28];

      memset(buf, 0, sizeof(buf));

      if (kbdStoreRead(slot, offset, buf, n) != true)
      {
        memset(&p_data[2], 0, length - 2);
        p_data[2] = LINK_RC_FAIL;
        break;
      }
      p_data[2] = LINK_RC_OK;
      p_data[3] = n;
      memcpy(&p_data[4], buf, n);
      break;
    }

    case LINK_CMD_SLOT_BEGIN:
    {
      kbd_hdr_t hdr;

      memset(&hdr, 0, sizeof(hdr));
      hdr.vid      = (uint16_t)p_data[3] | ((uint16_t)p_data[4] << 8);
      hdr.pid      = (uint16_t)p_data[5] | ((uint16_t)p_data[6] << 8);
      hdr.data_len = (uint16_t)p_data[7] | ((uint16_t)p_data[8] << 8);
      memcpy(hdr.name, &p_data[9], 23);
      hdr.name[KBD_NAME_MAX-1] = 0;

      memset(&p_data[2], 0, length - 2);

      if (hdr.data_len > kbdStoreDataMax()) { p_data[2] = LINK_RC_RANGE; break; }

      kbdStoreStageBegin(&hdr);
      break;
    }

    case LINK_CMD_SLOT_DATA:
    {
      uint16_t offset = (uint16_t)p_data[2] | ((uint16_t)p_data[3] << 8);
      bool     ok     = kbdStoreStageData(offset, &p_data[4], length - 4);

      memset(&p_data[2], 0, length - 2);
      p_data[2] = ok ? LINK_RC_OK : LINK_RC_FAIL;
      break;
    }

    case LINK_CMD_SLOT_COMMIT:
    {
      uint8_t   slot = p_data[2];
      kbd_hdr_t hdr;
      bool      is_new = (kbdStoreGetHeader(slot, &hdr) != true);
      bool      ok     = kbdStoreStageCommit(slot);

      /*
       * ★ 새로 만든 SLOT 은 **쓰던 키맵을 물려받는다** (09-3).
       *
       *   빈 키맵으로 시작하면 키보드를 하나 늘릴 때마다 하던 설정을 처음부터
       *   다시 해야 한다. 기본은 다 같고 그 키보드에서만 다른 것을 고치는 쪽이
       *   맞다. 아직 프로파일을 안 옮긴 지금 베껴야 "쓰던 것" 이 원본이 된다 —
       *   그래서 kbdStoreReselect() **앞**이다.
       */
      if (ok == true && is_new == true) qmkProfileCopyTo((uint8_t)(slot + 1));

      /* 담자마자 반영되게 — 안 그러면 키보드를 뽑았다 꽂아야 한다 */
      if (ok == true) kbdStoreReselect();

      memset(&p_data[2], 0, length - 2);
      p_data[2] = ok ? LINK_RC_OK : LINK_RC_FAIL;
      p_data[3] = willReenum();
      break;
    }

    case LINK_CMD_SLOT_ERASE:
    {
      bool ok = kbdStoreErase(p_data[2]);

      if (ok == true) kbdStoreReselect();

      memset(&p_data[2], 0, length - 2);
      p_data[2] = ok ? LINK_RC_OK : LINK_RC_FAIL;
      p_data[3] = willReenum();
      break;
    }

    case LINK_CMD_BOARD_INFO:
    {
      memset(&p_data[2], 0, length - 2);
      p_data[2] = LINK_RC_OK;
      p_data[3] = LINK_CMD_VERSION;
      strncpy((char *)&p_data[4], _DEF_FIRMWATRE_VERSION, length - 5);
      break;
    }

    case LINK_CMD_HOST_INFO:
    {
      /* 꽂힌 키보드가 스스로 말하는 이름. vid/pid 만으로는 사람이 못 알아본다 */
      uint8_t want = p_data[2];
      uint8_t n    = 0;

      memset(&p_data[2], 0, length - 2);

#ifdef _USE_HW_USBH
      for (int k=0; k<LINK_SOURCE_MAX; k++)
      {
        usbh_hid_info_t info;

        if (usbhHidGetInfo(k, &info) != true) continue;
        if (info.is_connect != true) continue;
        if (n++ != want) continue;

        p_data[3] = 1;
        p_data[4] = (uint8_t)(info.vid & 0xFF);
        p_data[5] = (uint8_t)(info.vid >> 8);
        p_data[6] = (uint8_t)(info.pid & 0xFF);
        p_data[7] = (uint8_t)(info.pid >> 8);
        usbhHidGetProduct(info.dev_addr, (char *)&p_data[8], length - 8);
        break;
      }
#endif
      break;
    }

    case LINK_CMD_SEL_SET:
    {
      /* 지금 꽂힌 키보드가 쓸 SLOT 을 고정한다 (0xFF = 자동) */
      bool ok = kbdStoreSelectSlot(p_data[2]);

      memset(&p_data[2], 0, length - 2);
      p_data[2] = ok ? LINK_RC_OK : LINK_RC_FAIL;
      p_data[3] = willReenum();
      break;
    }

    default:
      /* 모르는 서브명령. 개수 자리를 0 으로 두고 그대로 돌려준다 */
      memset(&p_data[2], 0, length - 2);
      break;
  }

  /*
   * ★ 우리 도구가 말을 걸고 있는 동안은 재열거를 미룬다 (usb.c 주석 참고).
   *
   *   웹 마법사는 20ms 마다 물어본다. 그동안 끊으면 담고 고치는 내내 연결이
   *   떨어져서 쓸 수가 없다. 조용해지면 그때 끊긴다 — 사용자가 마법사를
   *   끝내고 VIA/Vial 로 가는 시점이라 오히려 딱 맞다.
   */
#ifdef _USE_HW_USB
  usbPostponeReenum();
#endif

  usbdHidSendRaw(p_data, length);
  return true;
}
