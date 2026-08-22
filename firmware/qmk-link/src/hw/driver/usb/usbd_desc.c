#include "usb.h"

#ifdef _USE_HW_USB
#include "tusb.h"
#include "pico/usb_reset.h"
#include "pico/unique_id.h"
#include "usbd_hid.h"


// pico_stdio_usb 를 쓰지 않고 descriptor 를 직접 만든다.
// 그쪽은 자체 descriptor 를 갖고 있어서 HID 와 공존할 수 없다.
//
// 인터페이스 배치
//
//   IF0  HID Keyboard   boot protocol, 리포트 ID 없음    EP IN  0x81
//   IF1  HID Extra      mouse/system/consumer/NKRO       EP IN  0x82
//   IF2  HID Raw        VIA · Vial (usage page 0xFF60)   EP IN  0x83 / OUT 0x03
//   IF3  CDC 제어       ┐ IAD 로 묶인다                   EP IN  0x84
//   IF4  CDC 데이터     ┘                                 EP IN  0x85 / OUT 0x05
//   IF5  Vendor RESET   picotool 호환. 제어 전송만
//
// ★ 부트 키보드가 IF0 이어야 한다.
//   일부 BIOS 가 IF0 만 본다. 02단계에서는 CDC 가 IF0 이었고 여기서 밀어냈다.
//   (wish-he 의 usb_desc.h 에 같은 함정이 기록되어 있다)
//
// 호스트 도구는 인터페이스 번호가 아니라 usage page 로 찾으므로 번호가 밀려도 상관없다.


enum
{
  ITF_NUM_KBD = 0,
  ITF_NUM_EXTRA,
  ITF_NUM_RAW,
  ITF_NUM_CDC,
  ITF_NUM_CDC_DATA,
  ITF_NUM_RESET,
  ITF_NUM_TOTAL
};

enum
{
  STR_ID_LANGID = 0,
  STR_ID_MANUF,
  STR_ID_PRODUCT,
  STR_ID_SERIAL,
  STR_ID_KBD,
  STR_ID_EXTRA,
  STR_ID_RAW,
  STR_ID_CDC,
  STR_ID_RESET,
};

#define EP_KBD_IN         0x81
#define EP_EXTRA_IN       0x82
#define EP_RAW_IN         0x83
#define EP_RAW_OUT        0x03
#define EP_CDC_NOTIF      0x84
#define EP_CDC_IN         0x85
#define EP_CDC_OUT        0x05

#define EP_KBD_SIZE       8
#define EP_EXTRA_SIZE     32
#define EP_RAW_SIZE       HID_RAW_REPORT_LEN
#define CDC_NOTIF_SIZE    8
#define CDC_DATA_SIZE     64

// 폴링 주기(ms). 키보드는 1ms.
#define EP_KBD_INTERVAL   1
#define EP_EXTRA_INTERVAL 1
#define EP_RAW_INTERVAL   1

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN     + \
                           TUD_HID_DESC_LEN * 2    + \
                           TUD_HID_INOUT_DESC_LEN  + \
                           TUD_CDC_DESC_LEN        + \
                           TUD_RPI_RESET_DESC_LEN)


//-- IF0 : 부트 키보드
//
// 리포트 ID 를 쓰지 않는다. 8바이트 고정이라 BIOS 도 그대로 읽는다.
//   [0] 모디파이어  [1] 예약  [2..7] 눌린 키 6개
//
static const uint8_t hid_desc_keyboard[] =
{
  TUD_HID_REPORT_DESC_KEYBOARD()
};


//-- IF1 : 확장 (마우스 / 시스템 / 컨슈머 / NKRO)
//
// 리포트 ID 로 나눠 쓴다. convex 의 EXK 인터페이스와 같은 구성이다.
//
static const uint8_t hid_desc_extra[] =
{
  TUD_HID_REPORT_DESC_MOUSE         ( HID_REPORT_ID(HID_REPORT_ID_MOUSE)    ),
  TUD_HID_REPORT_DESC_SYSTEM_CONTROL( HID_REPORT_ID(HID_REPORT_ID_SYSTEM)   ),
  TUD_HID_REPORT_DESC_CONSUMER      ( HID_REPORT_ID(HID_REPORT_ID_CONSUMER) ),

  // NKRO. TinyUSB 에 매크로가 없어 직접 쓴다.
  //   [0] 리포트 ID
  //   [1] 모디파이어 8비트
  //   [2..31] 키 비트맵 240비트   -> 리포트 ID 포함 32바이트로 딱 맞다
  0x05, 0x01,                       // Usage Page (Generic Desktop)
  0x09, 0x06,                       // Usage (Keyboard)
  0xA1, 0x01,                       // Collection (Application)
  0x85, HID_REPORT_ID_NKRO,         //   Report ID
  0x05, 0x07,                       //   Usage Page (Keyboard/Keypad)
  0x19, 0xE0,                       //   Usage Minimum (Left Control)
  0x29, 0xE7,                       //   Usage Maximum (Right GUI)
  0x15, 0x00,                       //   Logical Minimum (0)
  0x25, 0x01,                       //   Logical Maximum (1)
  0x95, 0x08,                       //   Report Count (8)
  0x75, 0x01,                       //   Report Size (1)
  0x81, 0x02,                       //   Input (Data, Variable, Absolute)
  0x19, 0x00,                       //   Usage Minimum (0)
  0x29, (HID_NKRO_KEY_BYTES*8 - 1), //   Usage Maximum (239)
  0x95, (HID_NKRO_KEY_BYTES*8),     //   Report Count (240)
  0x75, 0x01,                       //   Report Size (1)
  0x81, 0x02,                       //   Input (Data, Variable, Absolute)
  0xC0                              // End Collection
};

//-- IF2 : raw HID (VIA · Vial)
//
// usage page 0xFF60. 호스트 도구는 인터페이스 번호가 아니라 이걸로 찾는다.
// 04단계에서는 엔드포인트만 뚫어 두고 응답은 06단계에서 채운다.
//
static const uint8_t hid_desc_raw[] =
{
  0x06, 0x60, 0xFF,                 // Usage Page (Vendor Defined 0xFF60)
  0x09, 0x61,                       // Usage (Vendor Defined)
  0xA1, 0x01,                       // Collection (Application)
  0x09, 0x62,                       //   Usage (Vendor Defined) - to host
  0x15, 0x00,                       //   Logical Minimum (0)
  0x26, 0xFF, 0x00,                 //   Logical Maximum (255)
  0x95, HID_RAW_REPORT_LEN,         //   Report Count (32)
  0x75, 0x08,                       //   Report Size (8)
  0x81, 0x02,                       //   Input (Data, Variable, Absolute)
  0x09, 0x63,                       //   Usage (Vendor Defined) - from host
  0x15, 0x00,                       //   Logical Minimum (0)
  0x26, 0xFF, 0x00,                 //   Logical Maximum (255)
  0x95, HID_RAW_REPORT_LEN,         //   Report Count (32)
  0x75, 0x08,                       //   Report Size (8)
  0x91, 0x02,                       //   Output (Data, Variable, Absolute)
  0xC0                              // End Collection
};

//-- Device Descriptor
//
static const tusb_desc_device_t desc_device =
{
  .bLength            = sizeof(tusb_desc_device_t),
  .bDescriptorType    = TUSB_DESC_DEVICE,
  .bcdUSB             = 0x0200,

  // CDC + 다른 클래스를 같이 쓰려면 IAD 가 필요하다.
  // Windows 가 복합 장치를 제대로 잡으려면 이게 있어야 한다.
  .bDeviceClass       = TUSB_CLASS_MISC,
  .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
  .bDeviceProtocol    = MISC_PROTOCOL_IAD,
  .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

  .idVendor           = HW_USB_VID,
  .idProduct          = HW_USB_PID,
  .bcdDevice          = 0x0100,

  .iManufacturer      = STR_ID_MANUF,
  .iProduct           = STR_ID_PRODUCT,
  .iSerialNumber      = STR_ID_SERIAL,

  .bNumConfigurations = 1
};

const uint8_t *tud_descriptor_device_cb(void)
{
  return (const uint8_t *)&desc_device;
}


//-- Configuration Descriptor
//
static const uint8_t desc_configuration[] =
{
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, STR_ID_LANGID, CONFIG_TOTAL_LEN,
                        TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 250),

  // IF0 : 부트 키보드. bInterfaceProtocol 을 KEYBOARD 로 둬야 BIOS 가 쓴다.
  TUD_HID_DESCRIPTOR(ITF_NUM_KBD, STR_ID_KBD, HID_ITF_PROTOCOL_KEYBOARD,
                     sizeof(hid_desc_keyboard), EP_KBD_IN, EP_KBD_SIZE, EP_KBD_INTERVAL),

  // IF1 : 마우스 / 시스템 / 컨슈머 / NKRO
  TUD_HID_DESCRIPTOR(ITF_NUM_EXTRA, STR_ID_EXTRA, HID_ITF_PROTOCOL_NONE,
                     sizeof(hid_desc_extra), EP_EXTRA_IN, EP_EXTRA_SIZE, EP_EXTRA_INTERVAL),

  // IF2 : raw HID (VIA · Vial)
  TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_RAW, STR_ID_RAW, HID_ITF_PROTOCOL_NONE,
                           sizeof(hid_desc_raw), EP_RAW_OUT, EP_RAW_IN, EP_RAW_SIZE, EP_RAW_INTERVAL),

  // IF3 + IF4 : CDC
  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, STR_ID_CDC, EP_CDC_NOTIF, CDC_NOTIF_SIZE,
                     EP_CDC_OUT, EP_CDC_IN, CDC_DATA_SIZE),

  // IF5 : picotool 호환용 vendor RESET
  TUD_RPI_RESET_DESCRIPTOR(ITF_NUM_RESET, STR_ID_RESET),
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
  (void)index;
  return desc_configuration;
}


//-- String Descriptor
//
static char serial_str[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];

static const char *string_desc_arr[] =
{
  [STR_ID_LANGID]  = (const char[]){0x09, 0x04},   // English (0x0409)
  [STR_ID_MANUF]   = "baram",
  [STR_ID_PRODUCT] = _DEF_BOARD_NAME,
  [STR_ID_SERIAL]  = serial_str,
  [STR_ID_KBD]     = "QMK-LINK Keyboard",
  [STR_ID_EXTRA]   = "QMK-LINK Extra",
  [STR_ID_RAW]     = "QMK-LINK VIA",
  [STR_ID_CDC]     = "QMK-LINK CDC",
  [STR_ID_RESET]   = "Reset",
};

static uint16_t desc_str[32];

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
  uint8_t len;

  (void)langid;

  if (index == STR_ID_LANGID)
  {
    memcpy(&desc_str[1], string_desc_arr[STR_ID_LANGID], 2);
    len = 1;
  }
  else
  {
    const char *str;

    if (index >= (sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) return NULL;
    if (string_desc_arr[index] == NULL) return NULL;

    str = string_desc_arr[index];

    len = (uint8_t)strlen(str);
    if (len > (sizeof(desc_str)/sizeof(desc_str[0]) - 1))
      len = sizeof(desc_str)/sizeof(desc_str[0]) - 1;

    for (uint8_t i=0; i<len; i++)
    {
      desc_str[1 + i] = str[i];
    }
  }

  desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * len + 2));

  return desc_str;
}


const uint8_t *usbdHidGetReportDesc(uint8_t itf, uint16_t *p_len)
{
  switch(itf)
  {
    case HID_ITF_KEYBOARD: *p_len = sizeof(hid_desc_keyboard); return hid_desc_keyboard;
    case HID_ITF_EXTRA:    *p_len = sizeof(hid_desc_extra);    return hid_desc_extra;
    case HID_ITF_RAW:      *p_len = sizeof(hid_desc_raw);      return hid_desc_raw;
  }

  *p_len = 0;
  return NULL;
}

void usbdDescInit(void)
{
  // 보드마다 다른 시리얼 번호를 쓴다.
  // 여러 대를 꽂았을 때 호스트가 구분할 수 있어야 한다.
  pico_get_unique_board_id_string(serial_str, sizeof(serial_str));
}

#endif
