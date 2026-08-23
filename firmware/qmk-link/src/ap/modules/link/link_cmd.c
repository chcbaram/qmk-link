#include "link_cmd.h"
#include "link.h"
#include "usbd_hid.h"
#include "usbh.h"
#include <string.h>


bool linkCmdHandle(uint8_t *p_data, uint8_t length, bool allow_matrix)
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

      if (allow_matrix == true)
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

    default:
      /* 모르는 서브명령. 개수 자리를 0 으로 두고 그대로 돌려준다 */
      memset(&p_data[2], 0, length - 2);
      break;
  }

  usbdHidSendRaw(p_data, length);
  return true;
}
