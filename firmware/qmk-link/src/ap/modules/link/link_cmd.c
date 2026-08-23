#include "link_cmd.h"
#include "link.h"
#include "kbd_store.h"
#include "usbd_hid.h"
#include "usbh.h"
#include <string.h>


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
      bool ok = kbdStoreStageCommit(p_data[2]);

      /* 담자마자 반영되게 — 안 그러면 키보드를 뽑았다 꽂아야 한다 */
      if (ok == true) kbdStoreReselect();

      memset(&p_data[2], 0, length - 2);
      p_data[2] = ok ? LINK_RC_OK : LINK_RC_FAIL;
      break;
    }

    case LINK_CMD_SLOT_ERASE:
    {
      bool ok = kbdStoreErase(p_data[2]);

      if (ok == true) kbdStoreReselect();

      memset(&p_data[2], 0, length - 2);
      p_data[2] = ok ? LINK_RC_OK : LINK_RC_FAIL;
      break;
    }

    default:
      /* 모르는 서브명령. 개수 자리를 0 으로 두고 그대로 돌려준다 */
      memset(&p_data[2], 0, length - 2);
      break;
  }

  usbdHidSendRaw(p_data, length);
  return true;
}
