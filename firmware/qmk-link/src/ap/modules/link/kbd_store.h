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


bool     kbdStoreInit(void);

// 칸 하나의 머리말. 비었으면 false.
bool     kbdStoreGetHeader(uint8_t slot, kbd_hdr_t *p_hdr);

// vid/pid (+ hash) 로 칸을 찾는다. 없으면 -1.
//
// ★ hash 는 vid/pid 가 같은 키보드를 가르는 데 쓴다.
//   싸구려 키보드들이 같은 vid/pid 를 공유하고 0000 도 있다.
//   0 을 넘기면 vid/pid 만 본다.
int      kbdStoreFind(uint16_t vid, uint16_t pid, uint32_t hash);

bool     kbdStoreRead(uint8_t slot, uint32_t offset, uint8_t *p_data, uint16_t length);

// 머리말 + 데이터를 한 번에 굽는다. 칸을 먼저 지운다.
bool     kbdStoreWrite(uint8_t slot, const kbd_hdr_t *p_hdr, const uint8_t *p_data);

bool     kbdStoreErase(uint8_t slot);

uint16_t kbdStoreDataMax(void);   /* 한 칸에 들어가는 데이터 최대 길이 */
uint8_t  kbdStoreUsedCount(void);

// product string 등을 해시한다 (FNV-1a 32).
uint32_t kbdStoreHash(const uint8_t *p_data, uint16_t length);


#ifdef __cplusplus
 }
#endif

#endif /* KBD_STORE_H_ */
