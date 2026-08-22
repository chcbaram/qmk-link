#include "usb.h"

#ifdef _USE_HW_USB
#include "tusb.h"
#include "pico/usb_reset.h"
#include "pico/unique_id.h"


// pico_stdio_usb 를 쓰지 않고 descriptor 를 직접 만든다.
// 그쪽은 자체 descriptor 를 갖고 있어서 04단계의 HID 와 공존할 수 없다.
//
// 구성 (04단계에서 여기에 HID 3개가 추가된다)
//   IAD - CDC (comm + data)   CLI / 로그
//   Vendor RESET              picotool 호환 (pico_usb_reset 라이브러리가 처리)


enum
{
  ITF_NUM_CDC = 0,
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
  STR_ID_CDC,
  STR_ID_RESET,
};

#define EP_CDC_NOTIF      0x81
#define EP_CDC_OUT        0x02
#define EP_CDC_IN         0x82

#define CDC_NOTIF_SIZE    8
#define CDC_DATA_SIZE     64

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_RPI_RESET_DESC_LEN)


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

  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, STR_ID_CDC, EP_CDC_NOTIF, CDC_NOTIF_SIZE,
                     EP_CDC_OUT, EP_CDC_IN, CDC_DATA_SIZE),

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


void usbdDescInit(void)
{
  // 보드마다 다른 시리얼 번호를 쓴다.
  // 여러 대를 꽂았을 때 호스트가 구분할 수 있어야 한다.
  pico_get_unique_board_id_string(serial_str, sizeof(serial_str));
}

#endif
