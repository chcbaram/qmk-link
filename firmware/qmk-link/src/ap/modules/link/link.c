#include "link.h"


// HID boot keyboard 리포트
//   [0]    모디파이어 비트 (LeftCtrl..RightGUI = usage 0xE0..0xE7)
//   [1]    예약
//   [2..7] 눌려 있는 키의 usage. 순서는 보장되지 않는다
//
// usage 를 그대로 좌표로 쓰므로 변환 테이블이 필요 없다.

#define LINK_KEY_ERR_ROLLOVER   0x01    // 6키 초과. 키가 아니다
#define LINK_MOD_USAGE_BASE     0xE0    // 모디파이어의 usage 시작


static uint16_t matrix[LINK_MATRIX_ROWS];
static bool     is_changed = false;
static uint32_t set_count = 0;
static uint32_t key_count = 0;




bool linkInit(void)
{
  linkClear();
  return true;
}

void linkClear(void)
{
  for (int i=0; i<LINK_MATRIX_ROWS; i++)
  {
    if (matrix[i] != 0) is_changed = true;
    matrix[i] = 0;
  }
}

void linkSetKeyboardReport(const uint8_t *p_report, uint8_t len)
{
  uint16_t next[LINK_MATRIX_ROWS] = {0, };

  if (len < 8) return;

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

  key_count = 0;
  for (int i=0; i<LINK_MATRIX_ROWS; i++)
  {
    if (matrix[i] != next[i])
    {
      matrix[i]  = next[i];
      is_changed = true;
    }
    for (int b=0; b<16; b++) if (next[i] & (1U<<b)) key_count++;
  }
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
