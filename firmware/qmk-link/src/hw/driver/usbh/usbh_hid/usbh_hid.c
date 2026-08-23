#include "usbh_hid.h"

#ifdef _USE_HW_USBH
#include "pico/util/queue.h"


// core1 이 리포트를 넣고 core0 이 꺼낸다.
// pico_util 의 queue_t 는 스핀락을 써서 코어 간에 안전하다.
static queue_t report_queue;

static usbh_hid_info_t hid_info[CFG_TUH_HID];

static volatile uint32_t rx_count   = 0;
static volatile uint32_t drop_count = 0;
static bool is_init = false;




bool usbhHidInit(void)
{
  memset(hid_info, 0, sizeof(hid_info));

  queue_init(&report_queue, sizeof(usbh_hid_report_t), USBH_HID_QUEUE_MAX);

  is_init = true;
  return true;
}

bool usbhHidIsConnected(void)
{
  for (int i=0; i<CFG_TUH_HID; i++)
  {
    if (hid_info[i].is_connect) return true;
  }
  return false;
}

bool usbhHidGetInfo(uint8_t index, usbh_hid_info_t *p_info)
{
  if (index >= CFG_TUH_HID) return false;

  *p_info = hid_info[index];
  return true;
}

bool usbhHidGetReport(usbh_hid_report_t *p_report)
{
  if (is_init != true) return false;

  return queue_try_remove(&report_queue, p_report);
}

uint32_t usbhHidGetRxCount(void)
{
  return rx_count;
}

uint32_t usbhHidGetDropCount(void)
{
  return drop_count;
}


//-- TinyUSB host 콜백. 전부 core1 에서 불린다.
//

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      uint8_t const *desc_report, uint16_t desc_len)
{
  if (instance >= CFG_TUH_HID) return;

  hid_info[instance].is_connect   = true;
  hid_info[instance].dev_addr     = dev_addr;
  hid_info[instance].instance     = instance;
  hid_info[instance].itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

  tuh_vid_pid_get(dev_addr, &hid_info[instance].vid, &hid_info[instance].pid);

  /*
   * ★ 미디어키를 받으려면 여기서 리포트 디스크립터를 봐야 한다.
   *
   *   볼륨 · 재생 키는 키보드 인터페이스가 아니라 Consumer 페이지(0x0C)를 쓰는
   *   별도 인터페이스로 온다. 그쪽 bInterfaceProtocol 은 NONE 이라
   *   프로토콜만으로는 마우스와 구별되지 않는다.
   *
   *   report_id 가 0 이 아니면 리포트 첫 바이트가 ID 다 — 값을 꺼낼 때 건너뛴다.
   */
  {
    tuh_hid_report_info_t info[4];
    uint8_t              n;

    n = tuh_hid_parse_report_descriptor(info, 4, desc_report, desc_len);

    for (uint8_t i=0; i<n; i++)
    {
      if (info[i].usage_page == HID_USAGE_PAGE_CONSUMER)
      {
        hid_info[instance].is_consumer = true;
        hid_info[instance].report_id   = info[i].report_id;
        hid_info[instance].usage_page  = info[i].usage_page;
        break;
      }
      if (i == 0)
      {
        hid_info[instance].report_id  = info[i].report_id;
        hid_info[instance].usage_page = info[i].usage_page;
      }
    }
  }

  // 리포트를 계속 받으려면 매번 다시 요청해야 한다.
  tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
  (void)dev_addr;

  if (instance >= CFG_TUH_HID) return;

  memset(&hid_info[instance], 0, sizeof(usbh_hid_info_t));
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                uint8_t const *report, uint16_t len)
{
  usbh_hid_report_t item;

  if (len > USBH_HID_REPORT_MAX) len = USBH_HID_REPORT_MAX;

  item.dev_addr = dev_addr;
  item.instance = instance;
  item.protocol = (instance < CFG_TUH_HID) ? hid_info[instance].itf_protocol : 0;
  item.is_consumer = (instance < CFG_TUH_HID) ? hid_info[instance].is_consumer : false;
  item.report_id   = (instance < CFG_TUH_HID) ? hid_info[instance].report_id   : 0;
  item.len      = (uint8_t)len;
  memcpy(item.data, report, len);

  rx_count++;

  // core0 이 안 가져가면 버린다. 여기서 막히면 USB 타이밍이 깨진다.
  if (queue_try_add(&report_queue, &item) != true)
  {
    drop_count++;
  }

  // 다음 리포트 요청. 이걸 빼먹으면 한 번만 받고 끝난다.
  tuh_hid_receive_report(dev_addr, instance);
}

#endif
