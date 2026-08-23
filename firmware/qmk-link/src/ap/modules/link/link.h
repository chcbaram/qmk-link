#ifndef LINK_H_
#define LINK_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "hw_def.h"


// USB-A 에 꽂힌 키보드가 보낸 HID 리포트를 "눌린 키 비트맵" 으로 바꾼다.
//
// 이 프로젝트는 물리 매트릭스가 없다. HID usage 를 그대로 좌표로 쓴다.
//
//   row = usage >> 4,  col = usage & 0x0F     ->  16 x 16 = 256 키
//
// QMK 쪽은 port/matrix.c 의 matrix_scan() 이 이 비트맵을 읽어간다.
// QMK 를 모르는 순수 로직이라 via / vial 트리가 같이 쓴다.

#define LINK_MATRIX_ROWS      16
#define LINK_MATRIX_COLS      16

// ★ 키보드가 여러 개 붙을 수 있다.
//
//   CFG_TUH_HUB 가 켜져 있어서 허브를 거쳐 두 대 이상이 붙는다.
//   인스턴스마다 비트맵을 따로 들고 OR 로 합친다. 안 그러면 한쪽이 보낸
//   리포트가 다른 쪽 키를 지운다 — 각 리포트는 "그 키보드의 전체 상태" 라
//   그대로 덮어쓰면 남의 키가 사라진다.
//
//   CFG_TUH_HID 이상이어야 한다.
#define LINK_SOURCE_MAX       4


bool     linkInit(void);

// boot keyboard 리포트 (8바이트) 를 반영한다. instance 마다 따로 기억한다.
void     linkSetKeyboardReport(uint8_t instance, const uint8_t *p_report, uint8_t len);

// 그 키보드가 빠졌을 때. 그 인스턴스의 키만 뗀다.
void     linkClearInstance(uint8_t instance);

// 전부 뗀다.
void     linkClear(void);

// matrix_scan() 이 읽는다. row 한 줄의 비트맵.
uint16_t linkGetRow(uint8_t row);

// 리포트가 들어와 비트맵이 바뀌었나 (matrix_scan 의 changed 반환용)
bool     linkIsChanged(void);
uint32_t linkGetSetCount(void);
uint32_t linkGetKeyCount(void);
void     linkClearChanged(void);


#ifdef __cplusplus
 }
#endif

#endif /* LINK_H_ */
