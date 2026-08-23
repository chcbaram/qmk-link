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
//     0x00 INFO     [2] 버전  [3] 트리(0=via 1=vial)  [4] 잠금(1=잠김)
//                   [5] 행  [6] 열  [7] 키보드 수
//                   [8..]  키보드마다 vid(2) pid(2)  little endian
//                   [28]   지금 고른 칸 (0xFF = 없음)      ← 버전 3
//                   [29..30] 지금 보고 중인 PID  little endian
//     0x01 PRESSED  [2] 개수 N  [3..] 눌린 usage N 개
//
//   ★ INFO 의 새 항목을 [8..] 뒤에 이어 붙이지 않고 **뒤쪽 고정 자리**에 둔다.
//     앞은 키보드 수만큼 길이가 변한다 (최대 8+4*4=24). 고정 자리에 두면
//     예전 도구가 읽던 [2..7] 이 그대로라 버전이 어긋나도 깨지지 않는다.
//
//   ── 레이아웃 저장소 (09단계 2) ──
//   슬롯 명령은 [2] 가 결과다 (0 = OK).
//
//     0x02 SLOT_INFO   req [2]슬롯
//                      rsp [2]결과 [3]사용중 [4..5]vid [6..7]pid
//                          [8..9]길이 [10..31]이름(22B, NUL 끝)
//     0x03 SLOT_READ   req [2]슬롯 [3..4]오프셋
//                      rsp [2]결과 [3]길이 [4..31]데이터(최대 28B)
//     0x04 SLOT_BEGIN  req [2]슬롯 [3..4]vid [5..6]pid [7..8]길이 [9..31]이름(23B)
//     0x05 SLOT_DATA   req [2..3]오프셋 [4..31]데이터(28B)
//     0x06 SLOT_COMMIT req [2]슬롯
//     0x07 SLOT_ERASE  req [2]슬롯
//
//   ★ 32바이트 리포트라 정의를 한 번에 못 보낸다. Begin -> Data 여러 번 ->
//     Commit 으로 나눈다. Commit 에서만 플래시를 건드린다 (소거 수명 때문이다).
//
// ★ 잠금 바이트는 **알림용**이다. 우리 PRESSED 는 잠겨 있어도 답한다.
//
//   Vial 은 매트릭스 읽기를 잠금 뒤로 숨긴다 (raw HID 키로거 방지).
//   우리는 그 문 하나만 연다 — via 빌드에서 VIA_INSECURE 로 이미 열어 둔 것과
//   같은 문이고, 이 보드는 "어떤 usage 가 오는가" 를 알아야 쓸 수 있기 때문이다.
//
//   ★ VIAL_INSECURE 로 통째로 푸는 것은 안 된다.
//     그 매크로는 id_bootloader_jump 케이스를 **컴파일에서 뺀다** —
//     Vial/VIA 앱의 부트로더 버튼이 죽는다. 매크로 쓰기와 QK_BOOT 심기도
//     같이 열린다. 그쪽은 잠가 둔 채로 둔다.
//
//   그래서 이 바이트는 "Vial 의 다른 기능(매크로 편집 등)이 잠겨 있는가" 다.
#define LINK_CMD_PREFIX       0xA0

#define LINK_CMD_INFO         0x00
#define LINK_CMD_PRESSED      0x01
#define LINK_CMD_SLOT_INFO    0x02
#define LINK_CMD_SLOT_READ    0x03
#define LINK_CMD_SLOT_BEGIN   0x04
#define LINK_CMD_SLOT_DATA    0x05
#define LINK_CMD_SLOT_COMMIT  0x06
#define LINK_CMD_SLOT_ERASE   0x07

#define LINK_RC_OK            0
#define LINK_RC_FAIL          1
#define LINK_RC_RANGE         2

#define LINK_CMD_VERSION      3

#define LINK_TREE_VIA         0
#define LINK_TREE_VIAL        1

// ★ 저장된 키보드마다 PID 를 다르게 보고한다 (VIA 가 정의를 자동으로 고르게).
//   0x5400 + 슬롯. 다른 baram 키보드(0x5200~0x5305)와 겹치지 않는 블록이다.
//   → firmware/docs/09-keyboard-profile.md
#define LINK_PID_BASE         0x5400


// 우리 명령이면 처리하고 응답까지 보낸 뒤 true. 아니면 아무것도 안 하고 false.
//
// vial_locked : Vial 의 잠금 상태 (알림용. via 빌드에서는 늘 false)
bool linkCmdHandle(uint8_t *p_data, uint8_t length, bool vial_locked);


#ifdef __cplusplus
 }
#endif

#endif /* LINK_CMD_H_ */
