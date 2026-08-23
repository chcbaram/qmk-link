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

/*
 * 꽂힌 키보드가 스스로 말하는 이름 (USB product string).
 *
 * ★ 왜 mount 때 바로 못 읽나
 *
 *   문자열은 컨트롤 전송으로 따로 물어봐야 한다. tuh_..._sync() 는 tuh_task()
 *   가 돌아야 끝나는데, 콜백 자체가 tuh_task() 안에서 불린다 — 거기서 sync 를
 *   부르면 자기가 끝나기를 자기가 기다린다. 그래서 **비동기**로 요청하고
 *   완료 콜백에서 채운다. 그 사이 잠깐 이름이 비어 있는 것은 정상이다.
 *
 * ★ 주소별로 둔다. 인터페이스(instance)가 아니라 장치(dev_addr) 것이다.
 *   한 키보드가 인터페이스를 여럿 낼 수 있고 이름은 하나다.
 */
static char    product_str[CFG_TUH_DEVICE_MAX + 1][USBH_PRODUCT_MAX];
static uint8_t product_want[CFG_TUH_DEVICE_MAX + 1];   /* 남은 재시도 횟수 */
static uint8_t product_buf[128];      /* UTF-16 원본. 한 번에 하나만 요청한다 */
static volatile bool product_busy = false;
static uint32_t product_try = 0;
static uint32_t product_ok  = 0;

/*
 * ★ mount 콜백에서 한 번 던지고 끝내면 안 된다 — 그게 처음에 이름이 안 뜬 이유다.
 *
 *   tuh_hid_mount_cb() 는 열거가 끝나는 길목에서 불린다. 그 시점에는 TinyUSB 가
 *   SET_IDLE / SET_PROTOCOL 같은 컨트롤 전송을 아직 쓰고 있어서, 컨트롤 슬롯이
 *   하나뿐인 usbh 가 우리 요청을 그냥 거절한다(false). 재시도가 없으니 이름은
 *   영영 안 온다 — 조용히 실패하는 종류다.
 *
 *   그래서 mount 때는 "받고 싶다" 는 표시만 하고, 실제 요청은 core1 루프에서
 *   50ms 마다 다시 던진다. 성공하면 표시를 지운다.
 */
#define PRODUCT_RETRY_MAX   40        /* 50ms x 40 = 2초쯤 */
#define PRODUCT_RETRY_US    50000

static void productDone(tuh_xfer_t *xfer)
{
  uint8_t daddr = (uint8_t)xfer->user_data;

  product_busy = false;

  if (daddr > CFG_TUH_DEVICE_MAX) return;
  if (xfer->result != XFER_RESULT_SUCCESS) return;   /* 남은 횟수만큼 다시 던진다 */

  /* [0] 전체 길이 [1] 0x03 [2..] UTF-16LE */
  {
    uint8_t  len = product_buf[0];
    uint8_t  n   = 0;

    if (len < 2 || len > sizeof(product_buf)) return;

    for (uint8_t i = 2; i + 1 < len && n < (USBH_PRODUCT_MAX - 1); i += 2)
    {
      uint16_t ch = (uint16_t)product_buf[i] | ((uint16_t)product_buf[i+1] << 8);

      /* ASCII 만 남긴다. 한글 이름은 여기서 물음표가 된다 */
      product_str[daddr][n++] = (ch >= 0x20 && ch < 0x7F) ? (char)ch : '?';
    }
    while (n > 0 && product_str[daddr][n-1] == ' ') n--;   /* 뒤 공백은 흔하다 */
    product_str[daddr][n] = 0;

    if (n > 0)
    {
      product_want[daddr] = 0;                  /* 받았다. 그만 던진다 */
      product_ok++;
    }
  }
}

static void productRequest(uint8_t daddr)
{
  if (daddr > CFG_TUH_DEVICE_MAX) return;
  if (product_str[daddr][0] != 0) return;       /* 이미 받았다 */

  product_want[daddr] = PRODUCT_RETRY_MAX;      /* 던지는 것은 usbhHidTask() 다 */
}

// core1 의 tuh_task() 뒤에서 부른다 (위 ★ 주석 참고).
void usbhHidTask(void)
{
  static uint32_t pre_time = 0;

  if (product_busy == true) return;
  if ((time_us_32() - pre_time) < PRODUCT_RETRY_US) return;
  pre_time = time_us_32();

  for (uint8_t d = 1; d <= CFG_TUH_DEVICE_MAX; d++)
  {
    if (product_want[d] == 0) continue;

    product_want[d]--;
    product_busy = true;
    product_try++;

    if (tuh_descriptor_get_product_string(d, 0x0409, product_buf,
                                          sizeof(product_buf),
                                          productDone, (uintptr_t)d) != true)
    {
      product_busy = false;                     /* 아직 바쁘다. 다음 차례에 */
    }
    return;                                     /* 한 번에 하나만 */
  }
}

void usbhHidGetProductStat(uint32_t *p_try, uint32_t *p_ok)
{
  if (p_try != NULL) *p_try = product_try;
  if (p_ok  != NULL) *p_ok  = product_ok;
}

bool usbhHidGetProduct(uint8_t dev_addr, char *p_str, uint8_t length)
{
  if (dev_addr > CFG_TUH_DEVICE_MAX) return false;
  if (product_str[dev_addr][0] == 0) return false;

  strncpy(p_str, product_str[dev_addr], length - 1);
  p_str[length - 1] = 0;
  return true;
}




bool usbhHidInit(void)
{
  memset(hid_info, 0, sizeof(hid_info));
  memset(product_str, 0, sizeof(product_str));
  memset(product_want, 0, sizeof(product_want));

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

  /* 이름은 따로 물어봐야 한다. 비동기다 (위 주석 참고) */
  productRequest(dev_addr);

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
  if (instance >= CFG_TUH_HID) return;

  memset(&hid_info[instance], 0, sizeof(usbh_hid_info_t));

  /* 그 장치의 마지막 인터페이스가 빠질 때만 이름을 지운다.
     한 키보드가 인터페이스를 여럿 낸다 (키보드 + 컨슈머 등) */
  if (dev_addr <= CFG_TUH_DEVICE_MAX)
  {
    for (int i=0; i<CFG_TUH_HID; i++)
      if (hid_info[i].is_connect && hid_info[i].dev_addr == dev_addr) return;

    product_str[dev_addr][0] = 0;
    product_want[dev_addr]   = 0;
  }
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
