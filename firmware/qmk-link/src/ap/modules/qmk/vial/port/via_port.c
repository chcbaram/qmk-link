/*
 * port/via_port.c — VIA 커스텀 메뉴
 *
 * VIA 는 정의 JSON 의 menus 를 보고 UI 를 스스로 그린다. 펌웨어는 채널 ID 별로
 * 값을 읽고 쓰기만 하면 된다. **VIA 웹앱을 포크할 필요가 없다.**
 *
 *   호스트 -> 장치   [0] id_custom_get/set_value  [1] 채널  [2] 값 ID  [3..] 값
 *
 * ★ 여기 있는 것들은 원래 QMK 의 빌드 옵션이다.
 *
 *   TAPPING_TERM 이나 HOLD_ON_OTHER_KEY_PRESS 를 config.h 에 박아 두면 바꿀 때마다
 *   다시 구워야 한다. 이 보드는 꽂는 키보드가 매번 달라서 그게 특히 불편하다.
 *   Vial 이 하는 것처럼 런타임 값으로 뺐다.
 *
 * ★ *_PER_KEY 매크로가 있어야 QMK 가 이 함수들을 부른다.
 *
 *   action_tapping.c 는 TAPPING_TERM_PER_KEY 가 없으면 get_tapping_term() 을
 *   아예 부르지 않고 컴파일 상수를 쓴다. keyboards/qmk-link/config.h 에서 켠다.
 *
 * ★ 채널 ID · 값 ID 는 keyboards/qmk-link/menus.json 과 같아야 한다.
 *   한쪽만 고치면 조용히 어긋난다 — 슬라이더를 움직여도 아무 일이 없다.
 */

#include "quantum.h"
#include "via.h"
#include "raw_hid.h"
#include "bootloader.h"
#include "eeconfig.h"
#include "eeprom.h"
#include "keycode_config.h"
#include "qmk.h"


#ifdef VIA_ENABLE

/* menus.json 의 채널 ID. 1~5 는 VIA 가 조명용으로 예약해 뒀다. */
enum
{
  CH_TAPHOLD = 14,
  CH_LINK    = 15,
};

/* menus.json 의 값 ID */
enum
{
  TH_TAPPING_TERM = 1,
  TH_HOLD_OKP,
  TH_PERMISSIVE_HOLD,
  TH_RETRO_TAPPING,
};

enum
{
  LK_NKRO = 1,
  LK_PASSTHROUGH,
};

#define TAPPING_TERM_MIN    50
#define TAPPING_TERM_MAX    500

/* 구조가 바뀌면 올린다. 옛 EEPROM 을 읽어 엉뚱한 값이 되는 것을 막는다. */
#define LINK_CFG_VERSION    1


typedef struct __attribute__((packed))
{
  uint8_t  version;
  uint16_t tapping_term;
  uint8_t  hold_okp;
  uint8_t  permissive_hold;
  uint8_t  retro_tapping;
  uint8_t  passthrough;
} link_cfg_t;


static link_cfg_t cfg;


static void cfgDefault(void)
{
  cfg.version         = LINK_CFG_VERSION;
  cfg.tapping_term    = TAPPING_TERM;
  cfg.hold_okp        = 0;
  cfg.permissive_hold = 0;
  cfg.retro_tapping   = 0;
  cfg.passthrough     = 0;
}

static void cfgApply(void)
{
  if (cfg.tapping_term < TAPPING_TERM_MIN) cfg.tapping_term = TAPPING_TERM_MIN;
  if (cfg.tapping_term > TAPPING_TERM_MAX) cfg.tapping_term = TAPPING_TERM_MAX;

  qmkSetPassthrough(cfg.passthrough != 0);
}

static void cfgSave(void)
{
  eeconfig_update_user_datablock(&cfg, 0, sizeof(cfg));
}


/*
 * EEPROM 의 user datablock 이 비었을 때 QMK 가 부른다.
 * (eeconfig 의 매직이 없거나 EECONFIG_USER_DATA_VERSION 이 다를 때)
 */
void eeconfig_init_user(void)
{
  cfgDefault();
  cfgSave();
}

void via_init_kb(void)
{
  cfgDefault();

  if (eeconfig_is_user_datablock_valid() == true)
  {
    eeconfig_read_user_datablock(&cfg, 0, sizeof(cfg));

    /* 옛 판이면 기본값으로 되돌리고 새로 쓴다 */
    if (cfg.version != LINK_CFG_VERSION)
    {
      cfgDefault();
      cfgSave();
    }
  }

  cfgApply();
}


//-- QMK 의 런타임 훅
//
// ★ vial 트리에서는 통째로 빠진다.
//
//   Vial 은 탭홀드 UI(QMK Settings)를 자기가 갖고 있고, vial.c / qmk_settings.c 가
//   get_tapping_term() · get_permissive_hold() 등을 정의한다.
//   여기서도 정의하면 중복 정의가 된다.
//
#ifndef VIAL_ENABLE
//
//   *_PER_KEY 를 켰기 때문에 QMK 가 키마다 이 함수들을 부른다.
//   지금은 키에 상관없이 같은 값을 준다 (키별 설정은 UI 를 만들 자리가 없다).
//

uint16_t get_tapping_term(uint16_t keycode, keyrecord_t *record)
{
  (void)keycode;
  (void)record;
  return cfg.tapping_term;
}

/*
 * ★ 퀵탭텀도 같이 따라가게 한다.
 *
 *   QMK 는 QUICK_TAP_TERM 을 안 주면 TAPPING_TERM 으로 잡는데 그건 컴파일 때
 *   200 으로 굳는다. 사용자가 탭텀을 150 으로 내려도 퀵탭텀은 200 으로 남아
 *   원래 지키려던 "퀵탭텀 <= 탭텀" 이 깨진다 (wish-he 에서 겪은 것).
 */
uint16_t get_quick_tap_term(uint16_t keycode, keyrecord_t *record)
{
  (void)keycode;
  (void)record;
  return cfg.tapping_term;
}

bool get_hold_on_other_key_press(uint16_t keycode, keyrecord_t *record)
{
  (void)keycode;
  (void)record;
  return cfg.hold_okp != 0;
}

bool get_permissive_hold(uint16_t keycode, keyrecord_t *record)
{
  (void)keycode;
  (void)record;
  return cfg.permissive_hold != 0;
}

bool get_retro_tapping(uint16_t keycode, keyrecord_t *record)
{
  (void)keycode;
  (void)record;
  return cfg.retro_tapping != 0;
}


#endif   /* !VIAL_ENABLE */


//-- VIA 커스텀 메뉴
//

static void viaSetValue(uint8_t channel, uint8_t *p_val)
{
  uint8_t id = p_val[0];

  if (channel == CH_TAPHOLD)
  {
    switch (id)
    {
      /*
       * ★ 탭텀은 두 바이트, 큰 자리가 먼저다.
       *   앱은 슬라이더 최댓값이 255 를 넘으면 자동으로 2바이트로 보낸다.
       */
      case TH_TAPPING_TERM:
        cfg.tapping_term = ((uint16_t)p_val[1] << 8) | p_val[2];
        break;

      case TH_HOLD_OKP:        cfg.hold_okp        = p_val[1] ? 1 : 0; break;
      case TH_PERMISSIVE_HOLD: cfg.permissive_hold = p_val[1] ? 1 : 0; break;
      case TH_RETRO_TAPPING:   cfg.retro_tapping   = p_val[1] ? 1 : 0; break;
      default: return;
    }
  }
  else if (channel == CH_LINK)
  {
    switch (id)
    {
      /* NKRO 는 우리 구조체가 아니라 QMK 의 eeconfig 에 산다 (NK_TOGG 와 같은 자리) */
      case LK_NKRO:
        keymap_config.nkro = p_val[1] ? 1 : 0;
        eeconfig_update_keymap(&keymap_config);
        clear_keyboard();
        return;

      case LK_PASSTHROUGH:     cfg.passthrough     = p_val[1] ? 1 : 0; break;
      default: return;
    }
  }
  else
  {
    return;
  }

  cfgApply();
}

static void viaGetValue(uint8_t channel, uint8_t *p_val)
{
  uint8_t id = p_val[0];

  if (channel == CH_TAPHOLD)
  {
    switch (id)
    {
      case TH_TAPPING_TERM:
        p_val[1] = (uint8_t)(cfg.tapping_term >> 8);
        p_val[2] = (uint8_t)(cfg.tapping_term & 0xFF);
        break;

      case TH_HOLD_OKP:        p_val[1] = cfg.hold_okp;        break;
      case TH_PERMISSIVE_HOLD: p_val[1] = cfg.permissive_hold; break;
      case TH_RETRO_TAPPING:   p_val[1] = cfg.retro_tapping;   break;
    }
  }
  else if (channel == CH_LINK)
  {
    switch (id)
    {
      case LK_NKRO:            p_val[1] = keymap_config.nkro;  break;
      case LK_PASSTHROUGH:     p_val[1] = cfg.passthrough;     break;
    }
  }
}

/*
 * ★ id_bootloader_jump 는 upstream via.c 에 구현이 없다.
 *
 *   via.h 에 enum(0x0B) 만 있고 switch 에 case 가 없다 — 키보드 쪽에서 처리하라는
 *   뜻이다. via_command_kb() 가 raw_hid_receive() 맨 앞에서 불리고, true 를 주면
 *   "응답까지 내가 다 했다" 는 의미다.
 *
 *   응답을 먼저 보내고 넘어간다. 안 그러면 앱이 타임아웃으로 오해한다.
 *   bootloader_jump() 는 돌아오지 않는다 (eeprom_flush() 후 reset_usb_boot()).
 */
bool via_command_kb(uint8_t *data, uint8_t length)
{
  if (data[0] != id_bootloader_jump) return false;

  raw_hid_send(data, length);

  /* 호스트가 응답을 가져갈 틈을 준다 */
  wait_ms(20);

  bootloader_jump();
  return true;
}

/*
 * via.c 가 조명 채널을 처리하고 남은 것을 여기로 넘긴다.
 * ★ 이 안에서 raw_hid_send() 를 부르면 안 된다 — via.c 가 응답을 보낸다.
 */
void via_custom_value_command_kb(uint8_t *data, uint8_t length)
{
  uint8_t *command_id = &data[0];
  uint8_t  channel_id = data[1];
  uint8_t *value      = &data[2];

  (void)length;

  if (channel_id != CH_TAPHOLD && channel_id != CH_LINK)
  {
    *command_id = id_unhandled;
    return;
  }

  switch (*command_id)
  {
    case id_custom_set_value:
      viaSetValue(channel_id, value);
      break;

    case id_custom_get_value:
      viaGetValue(channel_id, value);
      break;

    case id_custom_save:
      cfgSave();
      break;

    default:
      *command_id = id_unhandled;
      break;
  }
}

#endif
