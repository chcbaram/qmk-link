/*
 * port/vial_port.c — Vial 정의를 플래시에서 내준다
 *
 * ★ Vial 은 키보드 정의를 **장치에서 읽어간다**.
 *
 *     0xFE 0x01  vial_get_size   정의 길이
 *     0xFE 0x02  vial_get_def    32바이트씩 (page 번호로)
 *
 *   upstream 은 컴파일에 박힌 keyboard_definition[] 배열을 준다. 여기서
 *   가로채 **꽂힌 키보드에 맞는 칸**을 대신 주면, 키보드를 바꿔 꽂는 순간
 *   Vial 이 그 키보드를 그린다.
 *
 * ★ upstream 을 고치지 않는다.
 *
 *   vial_get_size / vial_get_def 는 vial.c 안에 박혀 있어 훅이 없다.
 *   대신 우리가 이미 raw HID 큐를 직접 비우고 있으므로(qmk.c 의 qmkUpdate)
 *   거기서 먼저 걷어낸다. link_cmd 와 같은 방식이다.
 *
 * ★ 저장된 칸이 없으면 아무것도 하지 않는다.
 *   그러면 upstream 이 컴파일에 박힌 기본 배열(풀사이즈)을 준다.
 */

#include "quantum.h"
#include "vial.h"
#include "via.h"
#include "raw_hid.h"
#include "kbd_store.h"
#include <string.h>


#ifdef VIAL_ENABLE


bool vialServeDefinition(uint8_t *p_data, uint8_t length)
{
  kbd_hdr_t hdr;
  int       slot;

  if (length < 2) return false;
  if (p_data[0] != id_vial_prefix) return false;
  if (p_data[1] != vial_get_size && p_data[1] != vial_get_def) return false;

  slot = kbdStoreGetActive();
  if (slot < 0) return false;                        /* 기본 배열을 쓴다 */
  if (kbdStoreGetHeader(slot, &hdr) != true) return false;

  if (p_data[1] == vial_get_size)
  {
    uint32_t sz = hdr.data_len;

    /* upstream 과 같은 형식 — msg[0..3] 에 길이를 little endian 으로 덮는다 */
    p_data[0] = (uint8_t)(sz      );
    p_data[1] = (uint8_t)(sz >>  8);
    p_data[2] = (uint8_t)(sz >> 16);
    p_data[3] = (uint8_t)(sz >> 24);
    memset(&p_data[4], 0, length - 4);
  }
  else
  {
    uint32_t page  = (uint32_t)p_data[2] | ((uint32_t)p_data[3] << 8);
    uint32_t start = page * VIAL_RAW_EPSIZE;
    uint32_t n     = VIAL_RAW_EPSIZE;

    if (start >= hdr.data_len) return false;         /* 범위 밖이면 upstream 에 맡긴다 */
    if (start + n > hdr.data_len) n = hdr.data_len - start;

    memset(p_data, 0, length);
    if (kbdStoreRead((uint8_t)slot, start, p_data, (uint16_t)n) != true) return false;
  }

  raw_hid_send(p_data, length);
  return true;
}


#endif
