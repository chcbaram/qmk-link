/*
 * port/matrix.c — 가상 매트릭스
 *
 * 이 보드에는 물리 매트릭스가 없다. USB-A 에 꽂힌 키보드가 보낸 HID 리포트를
 * ap/modules/link 가 비트맵으로 바꿔 두고, 여기서 그걸 읽어 QMK 에 넘긴다.
 *
 *   row = usage >> 4,  col = usage & 0x0F   ->  16 x 16 = 256 키
 *
 * 스캔이라는 게 없으므로 matrix_scan() 은 비트맵이 바뀌었는지만 본다.
 * 디바운스도 원본 키보드가 이미 했으므로 DEBOUNCE_TYPE 은 none 이다.
 */

#include "matrix.h"
#include "keyboard.h"
#include "util.h"
#include "link.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>


static matrix_row_t matrix[MATRIX_ROWS];




void matrix_init(void)
{
  memset(matrix, 0, sizeof(matrix));

  linkInit();

  matrix_init_kb();
}

uint8_t matrix_scan(void)
{
  bool changed;

  changed = linkIsChanged();

  if (changed)
  {
    linkClearChanged();

    for (uint8_t row = 0; row < MATRIX_ROWS; row++)
    {
      matrix[row] = (matrix_row_t)linkGetRow(row);
    }
  }

  matrix_scan_kb();

  return (uint8_t)changed;
}

matrix_row_t matrix_get_row(uint8_t row)
{
  if (row >= MATRIX_ROWS) return 0;

  return matrix[row];
}

void matrix_print(void)
{
}

/*
 * Vial 이 unlock 조합을 확인할 때 쓴다 (vial.c).
 * upstream 은 quantum/matrix_common.c 가 주는데 우리는 matrix.c 를 통째로
 * 대신하므로 여기서 준다.
 */
bool matrix_is_on(uint8_t row, uint8_t col)
{
  if (row >= MATRIX_ROWS || col >= MATRIX_COLS) return false;

  return (matrix[row] & ((matrix_row_t)1 << col)) != 0;
}


__attribute__((weak)) void matrix_init_kb(void)  { matrix_init_user(); }
__attribute__((weak)) void matrix_scan_kb(void)  { matrix_scan_user(); }
__attribute__((weak)) void matrix_init_user(void) {}
__attribute__((weak)) void matrix_scan_user(void) {}
