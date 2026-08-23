/*
 * flash.c — 내장 QSPI NOR 접근 (RP2350)
 *
 * 주소는 전부 **플래시 오프셋**이다. XIP 주소(0x10000000~)가 아니다.
 *
 *   ※ rp2040_fw 계열의 flash.c 는 반대로 XIP 절대주소를 받는다.
 *     여기서는 wish-he 의 flash.h 인터페이스를 따르므로 오프셋이다.
 *     그쪽 코드를 베껴 올 때 주소를 그대로 넘기면 안 된다.
 *
 * ★ 이 칩의 함정은 하나뿐이고, 대신 그게 크다 — core1 이다.
 *
 *   소거·기록 동안 XIP 가 멈춘다. 그 사이 코드를 플래시에서 인출하면 죽는다.
 *   core0 은 인터럽트만 막으면 되지만 **core1 은 PIO USB 를 돌고 있다**.
 *   rp2040_fw 처럼 __disable_irq() 만 해서는 core1 이 그대로 달려가 죽는다.
 *   그래서 pico_flash 의 flash_safe_execute() 를 쓴다 — core1 을 lockout 으로
 *   RAM 안에 세워 두고, 끝나면 풀어 준다.
 *
 *   조건이 하나 있다: core1 이 flash_safe_execute_core_init() 을 불러 뒀어야 한다.
 *   안 그러면 SDK 가 "저쪽이 뭘 하는지 모른다" 며 거절한다(PICO_ERROR_NOT_PERMITTED).
 *   → usbh.c 의 usbhCore1Main() 맨 앞에서 부른다.
 *
 * ★ 두 번째 함정 — p_data 가 플래시에 있으면 안 된다.
 *
 *   기록하는 동안 XIP 가 멈춘 상태라 원본도 못 읽는다. const 배열을 그대로
 *   넘기면 그 순간 죽는다. flashWrite() 가 먼저 막는다.
 *
 * 캐시 무효화는 따로 하지 않는다. SDK 의 flash_range_erase/program 이 끝에서
 * 부트롬의 flash_flush_cache 를 부른다.
 */

#include "flash.h"


#ifdef _USE_HW_FLASH

#include <string.h>

#include "cli.h"
#include "log.h"
#ifdef _USE_HW_USBH
#include "usbh/usbh.h"
#endif

#include "hardware/flash.h"
#include "pico/flash.h"


/* flash_safe_execute() 가 core1 과 손발을 맞추는 데 주는 시간 */
#define FLASH_LOCKOUT_TIMEOUT_MS    1000

#define FLASH_TBL_MAX               1


typedef struct
{
  uint32_t addr;
  uint32_t length;
} flash_tbl_t;

typedef struct
{
  uint32_t       addr;
  uint32_t       length;
  const uint8_t *p_data;
} flash_req_t;


/*
 * ★ 쓸 수 있는 영역은 이 표에 있는 것뿐이다.
 *
 *   주소를 잘못 넘기면 자기 펌웨어를 지운다. 그러면 다음 부팅부터 CLI 도 없어서
 *   원인을 못 찾는다. rp2040_fw 도 같은 표를 갖고 있는데, 그쪽 flashInSector() 는
 *   **겹치기만 하면** 통과시킨다 — 0x000000 부터 2MB 를 지우라고 해도 통과한다.
 *   여기서는 요청 구간이 표 안에 **완전히 들어갈 때만** 통과시킨다.
 */
static const flash_tbl_t flash_tbl[FLASH_TBL_MAX] =
    {
      { HW_FLASH_USER_BEGIN, HW_FLASH_SIZE - HW_FLASH_USER_BEGIN },
    };


#if CLI_USE(HW_FLASH)
static void cliFlash(cli_args_t *args);
#endif

static bool flashInRange(uint32_t addr, uint32_t length);
static bool flashProgram(uint32_t addr, const uint8_t *p_data, uint32_t length);
static int  flashExecute(void (*func)(void *), void *param);


static bool              is_init  = false;
static volatile bool     is_busy  = false;    /* 소거·기록 진행 중 */
static volatile uint32_t err_step = 0;        /* 1=range 2=erase 3=program */
static volatile uint32_t err_stat = 0;        /* flash_safe_execute() 의 반환값 */

static uint8_t           page_buf[HW_FLASH_PAGE_SIZE];   /* 부분 페이지 조립용 */




/* ─ 아래 두 함수는 XIP 가 멈춘 상태에서 불린다 ─
 *
 * flash_range_erase / flash_range_program 은 __no_inline_not_in_flash_func 로
 * RAM 에 올라가 있다. 여기서 다른 플래시 함수를 부르면 그 순간 죽는다.
 * logPrintf() 같은 것을 넣지 말 것.
 */
static void flashDoErase(void *param)
{
  const flash_req_t *p_req = (const flash_req_t *)param;

  flash_range_erase(p_req->addr, p_req->length);
}

static void flashDoWrite(void *param)
{
  const flash_req_t *p_req = (const flash_req_t *)param;

  flash_range_program(p_req->addr, p_req->p_data, p_req->length);
}


/*
 * ★ 플래시 작업은 전부 이걸 거친다.
 *
 *   flash_safe_execute() 가 core1 을 수십 ms 세운다. 그 사이 PIO USB 의 전송이
 *   끊기면 엔드포인트가 영구 에러 상태로 빠져 그 뒤로는 길이 0 리포트만 올라온다
 *   (키가 하나도 안 들어온다. 리셋해야 낫는다 — 실측).
 *
 *   ★ 이 프로젝트는 펌웨어를 통째로 RAM 에서 돌린다 (copy_to_ram).
 *     그래서 소거·기록 중에도 core1 은 멈추지 않는다 —
 *     PICO_FLASH_ASSUME_CORE1_SAFE=1 로 lockout 을 껐다.
 *     core0 만 인터럽트를 막고 기다린다.
 */
static int flashExecute(void (*func)(void *), void *param)
{
  int ret;

  is_busy = true;
  ret = flash_safe_execute(func, param, FLASH_LOCKOUT_TIMEOUT_MS);
  is_busy = false;

  return ret;
}

bool flashInit(void)
{
  is_init = true;

#if CLI_USE(HW_FLASH)
  cliAdd("flash", cliFlash);
#endif

  return is_init;
}

bool flashInRange(uint32_t addr, uint32_t length)
{
  if (length == 0) return false;
  if (addr + length > HW_FLASH_SIZE) return false;   /* 오버플로 겸 범위 */

  for (int i=0; i<FLASH_TBL_MAX; i++)
  {
    uint32_t begin = flash_tbl[i].addr;
    uint32_t end   = flash_tbl[i].addr + flash_tbl[i].length;

    if (addr >= begin && (addr + length) <= end)
    {
      return true;
    }
  }

  return false;
}

bool flashRead(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  if (is_init != true) return false;
  if (addr + length > HW_FLASH_SIZE) return false;

  /* 읽기는 어디든 된다. XIP 로 메모리 맵에 올라와 있으니 그냥 읽는다. */
  memcpy(p_data, (const void *)(XIP_BASE + addr), length);

  return true;
}

bool flashErase(uint32_t addr, uint32_t length)
{
  flash_req_t req;
  int         ret;

  if (is_init != true) return false;

  /* 소거는 섹터 단위다. 경계를 벗어나면 옆집을 지운다 — 여기서 막는다. */
  if ((addr % HW_FLASH_SECTOR_SIZE) != 0)   return false;
  if ((length % HW_FLASH_SECTOR_SIZE) != 0) return false;

  if (flashInRange(addr, length) != true)
  {
    err_step = 1;
    return false;
  }

  req.addr   = addr;
  req.length = length;
  req.p_data = NULL;

  ret = flashExecute(flashDoErase, &req);

  if (ret != PICO_OK)
  {
    err_step = 2;
    err_stat = (uint32_t)ret;
    return false;
  }

  return true;
}

/* 페이지 정렬을 전제로 한 번에 기록한다. 정렬 처리는 flashWrite() 가 한다. */
bool flashProgram(uint32_t addr, const uint8_t *p_data, uint32_t length)
{
  flash_req_t req;
  int         ret;

  req.addr   = addr;
  req.length = length;
  req.p_data = p_data;

  ret = flashExecute(flashDoWrite, &req);

  if (ret != PICO_OK)
  {
    err_step = 3;
    err_stat = (uint32_t)ret;
    return false;
  }

  return true;
}

bool flashWrite(uint32_t addr, const uint8_t *p_data, uint32_t length)
{
  uint32_t index = 0;
  uint32_t offset;
  uint32_t mid_len;

  if (is_init != true) return false;

  if (flashInRange(addr, length) != true)
  {
    err_step = 1;
    return false;
  }

  /* ★ 원본이 플래시에 있으면 기록 중에 못 읽는다 (위 함정 참고) */
  if ((uint32_t)p_data >= XIP_BASE && (uint32_t)p_data < (XIP_BASE + HW_FLASH_SIZE))
  {
    err_step = 1;
    return false;
  }

  /* 앞쪽 부분 페이지 — 기존 내용을 읽어 덮어쓸 곳만 갈아 끼운다 */
  offset = addr % HW_FLASH_PAGE_SIZE;
  if (offset != 0)
  {
    uint32_t page_addr = addr - offset;
    uint32_t len       = HW_FLASH_PAGE_SIZE - offset;

    if (len > length) len = length;

    memcpy(page_buf, (const void *)(XIP_BASE + page_addr), HW_FLASH_PAGE_SIZE);
    memcpy(&page_buf[offset], p_data, len);

    if (flashProgram(page_addr, page_buf, HW_FLASH_PAGE_SIZE) != true) return false;
    index += len;
  }

  /* 가운데 정렬 구간 — 한 번에 쓴다 (lockout 도 한 번이다) */
  mid_len = ((length - index) / HW_FLASH_PAGE_SIZE) * HW_FLASH_PAGE_SIZE;
  if (mid_len > 0)
  {
    if (flashProgram(addr + index, &p_data[index], mid_len) != true) return false;
    index += mid_len;
  }

  /* 뒤쪽 부분 페이지 */
  if (index < length)
  {
    uint32_t page_addr = addr + index;
    uint32_t len       = length - index;

    memcpy(page_buf, (const void *)(XIP_BASE + page_addr), HW_FLASH_PAGE_SIZE);
    memcpy(page_buf, &p_data[index], len);

    if (flashProgram(page_addr, page_buf, HW_FLASH_PAGE_SIZE) != true) return false;
  }

  return true;
}

bool flashIsReady(void)
{
  return (is_init == true) && (is_busy == false);
}

uint32_t flashGetErrStep(void)
{
  return err_step;
}

uint32_t flashGetErrStatus(void)
{
  return err_stat;
}




#if CLI_USE(HW_FLASH)

/*
 * 디바이스 ID (0x90). rp2040_fw 에서 가져왔다 — 실제로 어떤 칩이 붙었는지 확인용.
 * 그쪽은 __disable_irq() 만 했지만 우리는 core1 때문에 lockout 안에서 해야 한다.
 */
static void flashDoReadId(void *param)
{
  uint8_t *p_id  = (uint8_t *)param;
  uint8_t  txbuf[6] = {0x90, 0, 0, 0, 0, 0};
  uint8_t  rxbuf[6] = {0, };

  flash_do_cmd(txbuf, rxbuf, 6);

  p_id[0] = rxbuf[4];   /* manufacturer */
  p_id[1] = rxbuf[5];   /* device */
}

void cliFlash(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    uint8_t id[2] = {0, };

    flashExecute(flashDoReadId, id);

    cliPrintf("dev id     : %02X %02X\n", id[0], id[1]);
    cliPrintf("flash size : %d KB\n", HW_FLASH_SIZE/1024);
    cliPrintf("sector     : %d\n", HW_FLASH_SECTOR_SIZE);
    cliPrintf("page       : %d\n", HW_FLASH_PAGE_SIZE);
    cliPrintf("ready      : %d\n", flashIsReady());
    cliPrintf("err step   : %d\n", (int)flashGetErrStep());
    cliPrintf("err stat   : %d\n", (int)flashGetErrStatus());
    cliPrintf("쓸 수 있는 영역\n");
    for (int i=0; i<FLASH_TBL_MAX; i++)
    {
      cliPrintf("  0x%06X ~ 0x%06X  %d KB\n",
                (unsigned)flash_tbl[i].addr,
                (unsigned)(flash_tbl[i].addr + flash_tbl[i].length),
                (int)(flash_tbl[i].length/1024));
    }
    cliPrintf("  via  e2p : 0x%06X %d KB\n",
              (unsigned)HW_FLASH_E2P_VIA_BEGIN,  (int)(HW_FLASH_E2P_VIA_SIZE/1024));
    cliPrintf("  vial e2p : 0x%06X %d KB\n",
              (unsigned)HW_FLASH_E2P_VIAL_BEGIN, (int)(HW_FLASH_E2P_VIAL_SIZE/1024));
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "read"))
  {
    uint32_t addr   = (uint32_t)args->getData(1);
    uint32_t length = (uint32_t)args->getData(2);
    uint8_t  buf[16];

    for (uint32_t i=0; i<length; i+=16)
    {
      uint32_t len = (length - i) > 16 ? 16 : (length - i);

      if (flashRead(addr + i, buf, len) != true)
      {
        cliPrintf("flashRead() fail\n");
        break;
      }

      cliPrintf("0x%06X : ", (unsigned)(addr + i));
      for (uint32_t j=0; j<len; j++) cliPrintf("%02X ", buf[j]);
      cliPrintf("\n");
    }
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "erase"))
  {
    uint32_t addr   = (uint32_t)args->getData(1);
    uint32_t length = (uint32_t)args->getData(2);
    uint32_t exe_time;
    bool     is_ok;

    exe_time = micros();
    is_ok    = flashErase(addr, length);
    exe_time = micros() - exe_time;

    cliPrintf("erase 0x%06X %d : %s, %d us\n",
              (unsigned)addr, (int)length, is_ok ? "OK" : "FAIL", (int)exe_time);
    ret = true;
  }

  /*
   * ★ 06단계 착수 실험 — core1 을 잠근 동안 USB 가 얼마나 멈추는가
   *
   * usbh 의 task 카운트는 core1 이 tuh_task() 를 돈 횟수다. 잠긴 동안은 늘지 않는다.
   * 소거·기록에 걸린 시간과 그 사이 멈춘 core1 을 같이 본다.
   */
  if (args->argc == 1 && args->isStr(0, "test"))
  {
    uint32_t addr = HW_FLASH_SIZE - HW_FLASH_SECTOR_SIZE;   /* 예약 영역 마지막 섹터 */
    static uint8_t buf[HW_FLASH_SECTOR_SIZE];
    uint32_t t_erase;
    uint32_t t_write;
    uint32_t task_pre   = 0;
    uint32_t task_erase = 0;
    uint32_t task_write = 0;
    uint8_t  rd[16];

    for (int i=0; i<HW_FLASH_SECTOR_SIZE; i++) buf[i] = (uint8_t)i;

    cliPrintf("addr 0x%06X (예약 영역 마지막 섹터)\n", (unsigned)addr);

#ifdef _USE_HW_USBH
    task_pre = usbhGetTaskCount();
#endif

    t_erase = micros();
    if (flashErase(addr, HW_FLASH_SECTOR_SIZE) != true) cliPrintf("erase fail\n");
    t_erase = micros() - t_erase;
#ifdef _USE_HW_USBH
    task_erase = usbhGetTaskCount();
#endif

    t_write = micros();
    if (flashWrite(addr, buf, HW_FLASH_SECTOR_SIZE) != true) cliPrintf("write fail\n");
    t_write = micros() - t_write;
#ifdef _USE_HW_USBH
    task_write = usbhGetTaskCount();
#endif

    cliPrintf("erase 4KB : %6d us  (그 사이 core1 tuh_task %d 회)\n",
              (int)t_erase, (int)(task_erase - task_pre));
    cliPrintf("write 4KB : %6d us  (그 사이 core1 tuh_task %d 회)\n",
              (int)t_write, (int)(task_write - task_erase));
    cliPrintf("합계      : %6d us\n", (int)(t_erase + t_write));

    flashRead(addr, rd, 16);
    cliPrintf("verify    : ");
    for (int i=0; i<16; i++) cliPrintf("%02X ", rd[i]);
    cliPrintf("%s\n", (rd[0] == 0x00 && rd[15] == 0x0F) ? " OK" : " FAIL");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("flash info\n");
    cliPrintf("flash read  addr length\n");
    cliPrintf("flash erase addr length\n");
    cliPrintf("flash test\n");
  }
}
#endif

#endif
