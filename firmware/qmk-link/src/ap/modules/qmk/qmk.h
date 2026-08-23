#ifndef QMK_H_
#define QMK_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "ap_def.h"


// CLI 만 먼저 등록한다. 기동은 qmkStart() 가 한다 (ap.c 의 apInit).
bool qmkCliInit(void);

// QMK 코어를 올린다. 성공하면 true.
bool qmkStart(void);
bool qmkIsOn(void);

// 패스스루 — QMK 키 처리를 건너뛰고 원본 리포트를 그대로 PC 로 보낸다.
// VIA 커스텀 메뉴에서 켠다 (via/port/via_port.c). 키맵이 꼬였을 때의 탈출구다.
void qmkSetPassthrough(bool enable);
bool qmkIsPassthrough(void);

// ── 키맵 프로파일 (09-3) ──
//
// 꽂은 키보드마다 키맵을 따로 쓴다. 프로파일 = 적용 SLOT + 1 (SLOT 없으면 0).
// 실제 구현은 트리의 port/platforms/eeprom.c 다 — 여기서는 넘겨만 준다.
void    qmkSetProfile(uint8_t profile);
uint8_t qmkGetProfile(void);

// 지금 쓰는 키맵을 다른 프로파일로 베낀다. SLOT 을 새로 만들 때 부른다.
void    qmkProfileCopyTo(uint8_t profile);

// 패스스루 — QMK 키 처리를 건너뛰고 원본 리포트를 그대로 PC 로 보낸다.
// VIA 커스텀 메뉴에서 켠다 (via/port/via_port.c). 키맵이 꼬였을 때의 탈출구다.
void qmkSetPassthrough(bool enable);
bool qmkIsPassthrough(void);

// ── 키맵 프로파일 (09-3) ──
//
// 꽂은 키보드마다 키맵을 따로 쓴다. 프로파일 = 적용 SLOT + 1 (SLOT 없으면 0).
// 실제 구현은 트리의 port/platforms/eeprom.c 다 — 여기서는 넘겨만 준다.
void    qmkSetProfile(uint8_t profile);
uint8_t qmkGetProfile(void);

// 지금 쓰는 키맵을 다른 프로파일로 베낀다. SLOT 을 새로 만들 때 부른다.
void    qmkProfileCopyTo(uint8_t profile);

// 메인 루프에서 부른다. QMK 가 안 올라왔으면 아무것도 안 한다.
void qmkUpdate(void);


#ifdef __cplusplus
 }
#endif

#endif /* QMK_H_ */
