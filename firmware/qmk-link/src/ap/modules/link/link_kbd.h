#ifndef LINK_KBD_H_
#define LINK_KBD_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "ap_def.h"


/*
 * USB-A 에 꽂힌 키보드를 받아 넘기는 곳.
 *
 * ★ 이 프로젝트가 하는 일의 한가운데다.
 *
 *   core1 이 채워 둔 HID 리포트 큐를 꺼내
 *     · 키보드 리포트  -> link 의 가상 매트릭스 (QMK 가 읽어간다)
 *                        패스스루면 QMK 를 건너뛰고 그대로 PC 로
 *     · 마우스         -> 그대로 PC 로
 *     · 컨슈머(미디어) -> 그대로 PC 로 (QMK 의 키맵은 매트릭스 좌표라 못 태운다)
 *   로 나눠 보낸다.
 *
 *   꽂힌 키보드가 바뀌면 그 키보드의 레이아웃 SLOT 을 고르고, 그 결과를
 *   PC 쪽 PID 와 키맵 프로파일에 반영한다.
 *
 * ★ ap.c 에서 뺐다.
 *
 *   ap.c 는 진입점(apInit / apMain / cliLoopIdle)만 갖는다. 이 일은 300줄이
 *   넘고 진단 CLI 까지 딸려 있어서 진입점과 같은 파일에 둘 이유가 없다.
 */

bool linkKbdInit(void);

// 큐를 비우고 각자 갈 곳으로 보낸다. cliLoopIdle() 이 돌린다.
void linkKbdUpdate(void);

// 고른 SLOT 을 PC 쪽에 반영한다 — PID 와 키맵 프로파일.
//
// ★ qmkUpdate() **다음**에 불러야 한다. 레이아웃을 담는 raw HID 명령이 거기서
//   처리되면서 SLOT 이 바뀌기 때문이다 (link_kbd.c 의 주석 참고).
void linkKbdApplySlot(void);


#ifdef __cplusplus
 }
#endif

#endif /* LINK_KBD_H_ */
