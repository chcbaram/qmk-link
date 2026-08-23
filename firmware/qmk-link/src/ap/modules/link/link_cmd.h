#ifndef LINK_CMD_H_
#define LINK_CMD_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "hw_def.h"


// qmk-link 고유의 raw HID 명령.
//
// ★ upstream 훅에 기대지 않는다.
//
//   via_command_kb() 는 qmk_firmware 에만 있고 raw_hid_receive_kb() 는
//   vial-qmk 에만 있다. 어느 쪽에 붙여도 반대편에서 죽은 코드가 된다
//   (실제로 한 번 겪었다 — 링커가 조용히 버려서 빌드 크기가 안 변했다).
//
//   대신 우리가 이미 raw HID 큐를 직접 비우고 있으므로(qmk.c 의 qmkUpdate),
//   거기서 우리 명령만 먼저 걷어낸다. 두 트리가 똑같이 동작한다.
//
// ★ 왜 필요한가 — 표준 매트릭스 조회로는 안 되기 때문이다.
//
//   VIA 판은 offset 페이징이 있어 되지만, vial-qmk 판은 페이징이 없고
//   (cols/8+1)*rows <= 28 제한이 있어 우리 16x16(48)은 코드가 통째로 빠진다.
//   → firmware/docs/07-vial.md
//
//   그리고 우리에게 필요한 것은 비트맵 전체가 아니라 **지금 눌린 usage 목록**
//   뿐이다. 한 리포트에 넉넉히 들어간다 (최대 6키 + 모디파이어 8개).
//
//   프로토콜:
//     요청  [0] 0xA0  [1] 서브명령
//     응답  [0] 0xA0  [1] 서브명령  [2..] 결과
//
//     0x00 INFO     [2] 버전 [3] 행 [4] 열 [5] 키보드 수
//                   [6..]  키보드마다 vid(2) pid(2)  little endian
//     0x01 PRESSED  [2] 개수 N  [3..] 눌린 usage N 개
#define LINK_CMD_PREFIX       0xA0

#define LINK_CMD_INFO         0x00
#define LINK_CMD_PRESSED      0x01

#define LINK_CMD_VERSION      1


// 우리 명령이면 처리하고 응답까지 보낸 뒤 true. 아니면 아무것도 안 하고 false.
//
// allow_matrix : 눌린 키를 알려줘도 되는가.
//   vial 빌드에서는 Vial 의 잠금 정책을 따른다 (vial_unlocked).
//   잠겨 있으면 개수 0 으로 답한다 — 명령 자체는 응답해야 앱이 안 멈춘다.
bool linkCmdHandle(uint8_t *p_data, uint8_t length, bool allow_matrix);


#ifdef __cplusplus
 }
#endif

#endif /* LINK_CMD_H_ */
