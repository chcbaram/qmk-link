#ifndef QMK_H_
#define QMK_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "ap_def.h"


// CLI 만 먼저 등록한다. 기동은 qmkStart() 가 한다.
bool qmkCliInit(void);

// QMK 코어를 올린다. 성공하면 true.
bool qmkStart(void);
bool qmkIsOn(void);

// 메인 루프에서 부른다. QMK 가 안 올라왔으면 아무것도 안 한다.
void qmkUpdate(void);


#ifdef __cplusplus
 }
#endif

#endif /* QMK_H_ */
