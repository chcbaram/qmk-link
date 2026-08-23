#include "link.h"
#include <string.h>


// HID boot keyboard 리포트
//   [0]    모디파이어 비트 (LeftCtrl..RightGUI = usage 0xE0..0xE7)
//   [1]    예약
//   [2..7] 눌려 있는 키의 usage. 순서는 보장되지 않는다
//
// usage 를 그대로 좌표로 쓰므로 변환 테이블이 필요 없다.

#define LINK_KEY_ERR_ROLLOVER   0x01    // 6키 초과. 키가 아니다
#define LINK_MOD_USAGE_BASE     0xE0    // 모디파이어의 usage 시작


static uint16_t matrix[LINK_MATRIX_ROWS];                       // 합친 결과
static uint16_t src_matrix[LINK_SOURCE_MAX][LINK_MATRIX_ROWS];  // 키보드별
static bool     is_changed = false;
static uint32_t set_count = 0;
static uint32_t key_count = 0;




bool linkInit(void)
{
  linkClear();
  return true;
}

/* src_matrix 를 OR 로 합쳐 matrix 에 넣는다. 바뀌면 is_changed 를 세운다. */
static void linkMerge(void)
{
  key_count = 0;

  for (int r=0; r<LINK_MATRIX_ROWS; r++)
  {
    uint16_t bits = 0;

    for (int s=0; s<LINK_SOURCE_MAX; s++) bits |= src_matrix[s][r];

    if (matrix[r] != bits)
    {
      matrix[r]  = bits;
      is_changed = true;
    }
    for (int b=0; b<16; b++) if (bits & (1U<<b)) key_count++;
  }
}

void linkClearInstance(uint8_t instance)
{
  if (instance >= LINK_SOURCE_MAX) return;

  memset(src_matrix[instance], 0, sizeof(src_matrix[instance]));
  linkMerge();
}

void linkClear(void)
{
  memset(src_matrix, 0, sizeof(src_matrix));
  linkMerge();
}

void linkSetKeyboardReport(uint8_t instance, const uint8_t *p_report, uint8_t len)
{
  uint16_t next[LINK_MATRIX_ROWS] = {0, };

  if (len < 8) return;
  if (instance >= LINK_SOURCE_MAX) return;

  set_count++;

  // 모디파이어. 비트 위치가 usage 0xE0~0xE7 에 대응한다.
  for (int i=0; i<8; i++)
  {
    if (p_report[0] & (1<<i))
    {
      uint8_t usage = LINK_MOD_USAGE_BASE + i;
      next[usage >> 4] |= (1U << (usage & 0x0F));
    }
  }

  // 눌린 키
  for (int i=2; i<8; i++)
  {
    uint8_t usage = p_report[i];

    if (usage == 0) continue;
    if (usage == LINK_KEY_ERR_ROLLOVER) continue;

    next[usage >> 4] |= (1U << (usage & 0x0F));
  }

  // ★ 덮어쓰는 것은 이 키보드 몫뿐이다. 합치기는 linkMerge() 가 한다.
  memcpy(src_matrix[instance], next, sizeof(next));
  linkMerge();
}

uint32_t linkGetSetCount(void) { return set_count; }
uint32_t linkGetKeyCount(void) { return key_count; }

uint16_t linkGetRow(uint8_t row)
{
  if (row >= LINK_MATRIX_ROWS) return 0;

  return matrix[row];
}

bool linkIsChanged(void)
{
  return is_changed;
}

void linkClearChanged(void)
{
  is_changed = false;
}
