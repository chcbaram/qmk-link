#ifndef KBD_STORE_H_
#define KBD_STORE_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "hw_def.h"


// 꽂힌 키보드마다 레이아웃 정의를 플래시에 담아 둔다.
//
// ★ 왜 보드에 두는가
//
//   Vial 은 정의를 **장치에서 읽어간다** (vial_get_size / vial_get_def).
//   그래서 여기 담아 두고 꽂힌 키보드에 맞는 것을 내주면, 키보드를 바꿔 꽂는
//   순간 Vial 이 그 키보드를 그린다. VIA 는 그 통로가 없어 PID 를 바꾸는 방식을
//   쓴다 → firmware/docs/09-keyboard-profile.md
//
// 한 칸이 8KB 다 (섹터 2개). 압축된 Vial 정의가 실측 552B 라 넉넉하다.

#define KBD_SLOT_MAX      HW_FLASH_KBD_SLOT_MAX
#define KBD_NAME_MAX      32
#define KBD_MAGIC         0x4C4B4C51UL     /* "QLKL" */
#define KBD_VERSION       1


typedef struct __attribute__((packed))
{
  uint32_t magic;          /* KBD_MAGIC. 아니면 빈 칸이다 */
  uint8_t  version;
  uint8_t  reserved;
  uint16_t data_len;       /* 뒤따르는 정의 blob 의 길이 */
  uint16_t vid;
  uint16_t pid;
  uint32_t hash;           /* product string 해시. 0 이면 vid/pid 만 본다 */
  char     name[KBD_NAME_MAX];
} kbd_hdr_t;               /* 48 B */


// ── 키보드마다 "어느 SLOT 을 쓸지" 를 기억한다 ──
//
// ★ 왜 필요한가
//
//   같은 키보드를 여러 SLOT 에 담을 수 있다 (지금 쓰는 것 / 고치는 중인 것).
//   그런데 kbdStoreFind() 는 첫 일치를 집으므로 **번호가 곧 우선순위**가 되고
//   사용자는 못 고른다. 그 선택을 기록해 두는 표다.
//
//   기록이 유효하면 그것을, 없거나 못 쓰게 됐으면 지금까지처럼 첫 일치를 쓴다.
//   그래서 이 기능이 생기기 전에 담아 둔 보드도 그대로 동작한다.
//
// ★ 09-3 키맵 프로파일도 같은 표에 얹는다 (profile 바이트).

#define KBD_SEL_MAGIC     0x5351U      /* "QS" */
#define KBD_SEL_AUTO      0xFF         /* 자동 — 첫 일치를 쓴다 */

typedef struct __attribute__((packed))
{
  uint16_t vid;
  uint16_t pid;
  uint32_t hash;
  uint8_t  slot;           /* 적용할 SLOT. KBD_SEL_AUTO 면 자동 */
  uint8_t  profile;        /* 09-3 자리. 지금은 0 */
  uint16_t magic;          /* KBD_SEL_MAGIC. 아니면 빈 자리다 */
  uint32_t reserved;
} kbd_sel_t;               /* 16 B */


bool     kbdStoreInit(void);

// 칸 하나의 머리말. 비었으면 false.
bool     kbdStoreGetHeader(uint8_t slot, kbd_hdr_t *p_hdr);

// vid/pid (+ hash) 로 칸을 찾는다. 없으면 -1.
//
// ★ hash 는 vid/pid 가 같은 키보드를 가르는 데 쓴다.
//   싸구려 키보드들이 같은 vid/pid 를 공유하고 0000 도 있다.
//   0 을 넘기면 vid/pid 만 본다.
//
// ★ 같은 키보드가 여러 칸에 있으면 **번호가 낮은 칸이 이긴다.**
//   (hash 까지 맞는 칸이 있으면 그것이 먼저다)
//   진 칸은 영영 안 쓰인다 — 지워도 되는 데이터다. 눈에 안 보이면 "담았는데
//   왜 안 바뀌지" 가 되므로, 웹 마법사와 `kbd info` 가 가려진 칸을 표시한다.
int      kbdStoreFind(uint16_t vid, uint16_t pid, uint32_t hash);

bool     kbdStoreRead(uint8_t slot, uint32_t offset, uint8_t *p_data, uint16_t length);

// 머리말 + 데이터를 한 번에 굽는다. 칸을 먼저 지운다.
bool     kbdStoreWrite(uint8_t slot, const kbd_hdr_t *p_hdr, const uint8_t *p_data);

bool     kbdStoreErase(uint8_t slot);

// ── 조각으로 받아 조립한 뒤 한 번에 굽는다 ──
//
// raw HID 리포트가 32바이트뿐이라 정의를 한 번에 못 보낸다.
// Begin 으로 시작해 Data 를 여러 번 채우고 Commit 에서 굽는다.
void     kbdStoreStageBegin(const kbd_hdr_t *p_hdr);
bool     kbdStoreStageData(uint16_t offset, const uint8_t *p_data, uint8_t length);
bool     kbdStoreStageCommit(uint8_t slot);

uint16_t kbdStoreDataMax(void);   /* 한 칸에 들어가는 데이터 최대 길이 */
uint8_t  kbdStoreUsedCount(void);

// product string 등을 해시한다 (FNV-1a 32).
uint32_t kbdStoreHash(const uint8_t *p_data, uint16_t length);


// ── 지금 꽂힌 키보드에 맞는 칸 ──
//
// 소스 키보드가 바뀌면 ap.c 가 kbdStoreSelect() 를 부른다.
// Vial 정의 서빙과 PID 전환이 이 값을 본다.
void     kbdStoreSelect(uint16_t vid, uint16_t pid, uint32_t hash);
int      kbdStoreGetActive(void);        /* -1 = 맞는 칸이 없다 (기본 배열을 쓴다) */

// 저장소가 바뀌었을 때 같은 키보드로 다시 고른다.
// 담자마자 반영되게 — 안 그러면 키보드를 뽑았다 꽂아야 한다.
void     kbdStoreReselect(void);

// 지금 꽂힌 키보드가 쓸 SLOT 을 정해 기록한다 (KBD_SEL_AUTO = 자동).
// 바로 반영된다.
bool     kbdStoreSelectSlot(uint8_t slot);

// 지금 꽂힌 키보드에 기록된 선택. -1 = 기록 없음(자동).
int      kbdStoreGetSelected(void);

// 표를 직접 보는 쪽 (CLI · 진단용)
bool     kbdSelGet(uint16_t vid, uint16_t pid, uint32_t hash, uint8_t *p_slot);
bool     kbdSelSet(uint16_t vid, uint16_t pid, uint32_t hash, uint8_t slot);


#ifdef __cplusplus
 }
#endif

#endif /* KBD_STORE_H_ */
