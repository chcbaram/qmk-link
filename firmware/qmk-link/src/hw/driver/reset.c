/*
 * reset.c — 리셋 사유와 BOOTSEL 진입
 *
 * 헤더(common/hw/include/reset.h)는 다른 프로젝트와 공유하는 것을 그대로 쓴다.
 * 이 보드에서 실제로 쓰는 것만 구현한다.
 * 부팅 모드(resetSetBootMode / resetGetBootMode) 는 쓰는 곳이 없어서 구현하지 않았다.
 * 필요해지면 watchdog scratch[0..3] 에 두면 된다 — watchdog_reboot() 은 넘기지만
 * RUN 핀 리셋은 못 넘긴다 (RP2350 은 RUN 리셋 때 RAM/scratch 가 지워진다.
 * 실측 확인, docs/02-cli-cdc.md 참고).
 *
 * BOOTSEL 진입 경로는 네 가지고 이 파일은 CLI 만 담당한다.
 *   1. flash.py 의 CDC 1200bps touch   pico_usb_reset
 *   2. Key2(Reset) 빠르게 두 번        pico_bootsel_via_double_reset
 *   3. Key1(BOOTSEL) + Key2(Reset)     하드웨어
 *   4. CLI `reset boot`                아래
 */

#include "reset.h"

#ifdef _USE_HW_RESET
#include "cli.h"
#include "log.h"
#include "pico/bootrom.h"
#include "hardware/watchdog.h"
#include "hardware/structs/powman.h"


#ifdef _USE_HW_CLI
static void cliCmd(cli_args_t *args);
#endif


static bool     is_init    = false;
static uint32_t reset_bits = 0;
static uint32_t chip_reset = 0;    // 원본. 진단용으로 남긴다.


static const char *reset_bit_str[RESET_BIT_MAX] =
  {
    "RESET_BIT_POWER",
    "RESET_BIT_PIN",
    "RESET_BIT_WDG",
    "RESET_BIT_SOFT",
    "RESET_BIT_ETC",
  };




bool resetInit(void)
{
  // POWMAN_CHIP_RESET 의 HAD_* 는 읽기 전용이라 지울 필요가 없다.
  // 여러 개가 같이 설 수 있다.
  chip_reset = powman_hw->chip_reset;

  if (chip_reset & POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS)
  {
    reset_bits |= (1<<RESET_BIT_PIN);
  }
  if (chip_reset & (POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_SWCORE_BITS |
                    POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_PSM_BITS    |
                    POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_POWMAN_BITS))
  {
    reset_bits |= (1<<RESET_BIT_WDG);
  }
  if (chip_reset & POWMAN_CHIP_RESET_HAD_HZD_SYS_RESET_REQ_BITS)
  {
    reset_bits |= (1<<RESET_BIT_SOFT);
  }
  if (chip_reset & (POWMAN_CHIP_RESET_HAD_POR_BITS | POWMAN_CHIP_RESET_HAD_BOR_BITS))
  {
    reset_bits |= (1<<RESET_BIT_POWER);
  }
  if (chip_reset & (POWMAN_CHIP_RESET_HAD_GLITCH_DETECT_BITS |
                    POWMAN_CHIP_RESET_HAD_RESCUE_BITS        |
                    POWMAN_CHIP_RESET_HAD_DP_RESET_REQ_BITS  |
                    POWMAN_CHIP_RESET_HAD_SWCORE_PD_BITS))
  {
    reset_bits |= (1<<RESET_BIT_ETC);
  }

  is_init = true;

#ifdef _USE_HW_CLI
  cliAdd("reset", cliCmd);
#endif

  return is_init;
}

void resetLog(void)
{
  logPrintf("CHIP_RESET\t\t: 0x%08X\r\n", chip_reset);

  for (int i=0; i<RESET_BIT_MAX; i++)
  {
    if (reset_bits & (1<<i))
    {
      logPrintf("         \t\t  %s\r\n", reset_bit_str[i]);
    }
  }
}

void resetToBoot(void)
{
  // RP2350 은 BOOTROM 의 UF2 부트로더가 항상 살아 있다.
  reset_usb_boot(0, 0);
}

void resetToReset(void)
{
  watchdog_reboot(0, 0, 10);
  while(1);
}

uint32_t resetGetBits(void)
{
  return reset_bits;
}


#ifdef _USE_HW_CLI
void cliCmd(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("CHIP_RESET : 0x%08X\n", chip_reset);
    for (int i=0; i<RESET_BIT_MAX; i++)
    {
      if (reset_bits & (1<<i))
      {
        cliPrintf("      %s\n", reset_bit_str[i]);
      }
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "boot"))
  {
    cliPrintf("reboot to BOOTSEL...\n");
    delay(100);
    resetToBoot();
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "reset"))
  {
    cliPrintf("reboot...\n");
    delay(100);
    resetToReset();
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("reset info\n");
    cliPrintf("reset boot\n");
    cliPrintf("reset reset\n");
  }
}
#endif

#endif
