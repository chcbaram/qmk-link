#ifndef USBH_HID_H_
#define USBH_HID_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_USBH
#include "tusb.h"


typedef struct
{
  bool     is_connect;
  uint8_t  dev_addr;
  uint8_t  instance;
  uint8_t  itf_protocol;      // HID_ITF_PROTOCOL_NONE / KEYBOARD / MOUSE
  uint16_t vid;
  uint16_t pid;

  // ★ 미디어키(볼륨 · 재생)는 키보드 인터페이스로 오지 않는다.
  //
  //   HID Consumer 페이지(0x0C)를 쓰는 **별도 인터페이스**로 온다.
  //   그쪽은 bInterfaceProtocol 이 NONE 이라 프로토콜만 봐서는 구별이 안 되고,
  //   리포트 디스크립터를 파싱해야 안다 (mount 때 한 번).
  bool     is_consumer;       // Consumer Control 인터페이스인가
  uint8_t  report_id;         // 0 이면 리포트에 ID 바이트가 없다
  uint16_t usage_page;
} usbh_hid_info_t;


// USB-A 에서 올라온 HID 리포트 하나.
// core1 이 채우고 core0 이 꺼낸다.
typedef struct
{
  uint8_t dev_addr;
  uint8_t instance;
  uint8_t protocol;
  bool    is_consumer;      // Consumer Control 인터페이스에서 온 것인가
  uint8_t report_id;        // 0 이면 data[0] 부터 값이다
  uint8_t len;
  uint8_t data[USBH_HID_REPORT_MAX];
} usbh_hid_report_t;


bool usbhHidInit(void);
bool usbhHidIsConnected(void);
bool usbhHidGetInfo(uint8_t index, usbh_hid_info_t *p_info);

// 큐에서 리포트를 꺼낸다. 없으면 false. core0 에서 부른다.
bool usbhHidGetReport(usbh_hid_report_t *p_report);

uint32_t usbhHidGetRxCount(void);
uint32_t usbhHidGetDropCount(void);


#endif

#ifdef __cplusplus
 }
#endif

#endif /* USBH_HID_H_ */
