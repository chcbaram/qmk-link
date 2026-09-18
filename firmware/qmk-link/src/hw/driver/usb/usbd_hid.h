#ifndef USBD_HID_H_
#define USBD_HID_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_USB


// HID 인터페이스 (TinyUSB 의 instance 번호와 같아야 한다)
enum
{
  HID_ITF_KEYBOARD = 0,   // boot protocol. 리포트 ID 없음
  HID_ITF_EXTRA,          // mouse / system / consumer / NKRO. 리포트 ID 로 나눈다
  HID_ITF_RAW,            // VIA · Vial (usage page 0xFF60)
  HID_ITF_COUNT
};

// 리포트 ID. QMK 의 enum hid_report_ids 와 같아야 한다.
// 05단계에서 QMK 를 올릴 때 어긋나면 곤란하다.
enum
{
  HID_REPORT_ID_KEYBOARD = 1,
  HID_REPORT_ID_MOUSE,        // 2
  HID_REPORT_ID_SYSTEM,       // 3
  HID_REPORT_ID_CONSUMER,     // 4
  HID_REPORT_ID_PROGRAMMABLE, // 5
  HID_REPORT_ID_NKRO,         // 6
};

#define HID_NKRO_KEY_BYTES    30                        // 240키
#define HID_NKRO_REPORT_LEN   (1 + HID_NKRO_KEY_BYTES)  // 모디파이어 + 비트맵
#define HID_RAW_REPORT_LEN    32

// 못 보낸 리포트를 쌓아 두는 큐의 깊이 (usbd_hid.c 의 ★ 주석 참고)
#define USBD_HID_KBD_QUEUE_MAX    16
#define USBD_HID_EXTRA_QUEUE_MAX  16


// 진단용 — PC 로 나가는 키보드 리포트가 어디서 막히는지 본다.
typedef struct
{
  uint32_t try_cnt;
  uint32_t same_cnt;
  uint32_t busy_cnt;
  uint32_t sent_cnt;
  uint32_t fail_cnt;
  uint32_t retry_cnt;
  uint32_t wait_cnt;      // 큐가 꽉 차 기다린 횟수
  uint32_t over_cnt;      // 기다려도 안 빠져 마지막 칸을 덮은 횟수
  uint8_t  depth;         // 지금 큐에 쌓인 개수
  uint8_t  depth_max;     // 지금까지 최대
  bool     is_ready;
  bool     is_mount;
  bool     is_susp;
} usbd_hid_kbd_stat_t;

// 진단용 — IF1(Extra) 큐. NKRO · 마우스 · 컨슈머 · 시스템이 같이 탄다.
typedef struct
{
  uint32_t try_cnt;
  uint32_t sent_cnt;
  uint32_t busy_cnt;
  uint32_t fail_cnt;
  uint32_t wait_cnt;
  uint32_t over_cnt;
  uint8_t  depth;
  uint8_t  depth_max;
} usbd_hid_extra_stat_t;

void usbdHidGetKbdStat(usbd_hid_kbd_stat_t *p_stat);
void usbdHidGetExtraStat(usbd_hid_extra_stat_t *p_stat);


bool usbdHidInit(void);

// ★ 메인 루프에서 계속 부른다 (ap.c 의 cliLoopIdle).
//   큐에 쌓인 리포트를 엔드포인트가 빌 때마다 하나씩 꺼내 보낸다.
void usbdHidUpdate(void);

// 호스트가 붙어서 리포트를 받을 준비가 됐나
bool usbdHidIsReady(uint8_t itf);

// boot keyboard 리포트 8바이트를 큐에 넣는다 (usbd_hid.c 의 ★ 주석 참고).
// 직전에 **큐에 넣은 것**과 같으면 아무것도 하지 않는다.
bool usbdHidSendKeyboard(const uint8_t *p_report);

// IF1(Extra) 로 보낸다. 첫 바이트가 리포트 ID 다. 이쪽도 큐를 탄다.
// QMK 의 report_nkro_t / report_mouse_t / report_extra_t 를 그대로 넘긴다.
bool usbdHidSendExtra(const uint8_t *p_report, uint16_t len);

// 호스트가 SET_PROTOCOL 로 정한 값. 0 = boot, 1 = report
uint8_t usbdHidGetProtocol(void);

// 미디어키 (볼륨 · 재생 등). HID Consumer 페이지의 usage 를 그대로 넘긴다.
// usage 0 은 "뗌" 이다.
bool usbdHidSendConsumer(uint16_t usage);

// 마우스. buttons/x/y/wheel/pan
bool usbdHidSendMouse(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan);

// 호스트가 보낸 LED 상태 (CapsLock 등). HID_KEYBOARD_LED_* 비트
uint8_t usbdHidGetLed(void);

//-- raw HID (VIA · Vial)
//
// ★ 받는 쪽은 큐를 거친다.
//
//   OUT 리포트는 tud_task() 안의 콜백으로 들어온다. 거기서 바로 VIA 를 처리하면
//   dynamic keymap 기록과 응답 전송이 USB 콜백 안에서 벌어진다. 응답을 보내려면
//   IN 엔드포인트가 빌 때까지 기다려야 하는데 그 대기가 다시 tud_task() 를 부른다.
//   큐에 넣어 두고 메인 루프(qmkUpdate)에서 꺼내 처리한다.
bool usbdHidGetRaw(uint8_t *p_data);          // 32바이트. 없으면 false
bool usbdHidSendRaw(const uint8_t *p_data, uint16_t len);
uint32_t usbdHidGetRawRxCount(void);
uint32_t usbdHidGetRawDropCount(void);

const uint8_t *usbdHidGetReportDesc(uint8_t itf, uint16_t *p_len);


#endif

#ifdef __cplusplus
 }
#endif

#endif /* USBD_HID_H_ */
