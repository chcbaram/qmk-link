# QMK (Vial 트리)
#
# quantum 은 upstream 을 그대로 쓴다 (firm-sdk/upstream/vial-qmk).
# 우리 저장소에는 port 레이어와 키보드 정의만 있다.
#
# ★ vial-qmk 는 QMK 의 포크지만 베이스는 사실상 같다 — 확인했다:
#     host_driver_t 에 send_raw_hid 있음 / usb_device_state 있음 / quantum/nvm 있음
#   그래서 via 트리의 port/ 를 거의 그대로 옮겼다.
#   갈리는 것은 config.h(UID·unlock 조합)와 eeprom 영역뿐이다.

set(QMK_PORT_PATH     ${CMAKE_CURRENT_LIST_DIR})
set(QMK_QUANTUM_PATH  ${QMK_UPSTREAM_PATH}/quantum)

# 디바운스를 쓰지 않는다.
#
# 원본 키보드가 이미 디바운스한 결과가 USB 로 온다. 여기서 또 하면 지연만 는다.
# 애초에 우리 matrix_scan() 은 물리 매트릭스를 샘플링하는 게 아니라 마지막 리포트를
# 읽을 뿐이라 바운스할 대상이 없다. quantum/debounce/*.c 를 빌드에 넣지 않는다.
# (wish-he 도 같은 이유 — HE 스위치는 접점이 없다)

file(GLOB QMK_SRC_FILES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/ap/modules/link/*.c
  ${CMAKE_CURRENT_SOURCE_DIR}/ap/modules/qmk/qmk.c

  ${QMK_PORT_PATH}/port/*.c
  ${QMK_PORT_PATH}/port/platforms/*.c

  # 프로토콜 계층은 upstream 원본을 쓴다.
  # hola-mini / wish-he 는 참조할 upstream 이 없어 통째로 vendoring 했지만,
  # 우리는 받아 쓰므로 그럴 이유가 없다. 유지할 코드가 줄어든다.
  ${QMK_UPSTREAM_PATH}/tmk_core/protocol/host.c
  ${QMK_UPSTREAM_PATH}/tmk_core/protocol/report.c
  ${QMK_UPSTREAM_PATH}/tmk_core/protocol/usb_device_state.c

  ${QMK_QUANTUM_PATH}/nvm/eeprom/nvm_eeconfig.c
  ${QMK_QUANTUM_PATH}/nvm/eeprom/nvm_dynamic_keymap.c
  ${QMK_QUANTUM_PATH}/nvm/eeprom/nvm_via.c

  # VIA — raw HID 프로토콜과 동적 키맵. Vial 은 이 위에 얹힌다.
  ${QMK_QUANTUM_PATH}/via.c
  ${QMK_QUANTUM_PATH}/dynamic_keymap.c
  ${QMK_QUANTUM_PATH}/raw_hid.c

  # Vial
  ${QMK_QUANTUM_PATH}/vial.c

  ${QMK_QUANTUM_PATH}/quantum.c
  ${QMK_QUANTUM_PATH}/keyboard.c
  ${QMK_QUANTUM_PATH}/action.c
  ${QMK_QUANTUM_PATH}/action_tapping.c
  ${QMK_QUANTUM_PATH}/action_util.c
  ${QMK_QUANTUM_PATH}/action_layer.c
  ${QMK_QUANTUM_PATH}/keycode_config.c
  ${QMK_QUANTUM_PATH}/led.c
  ${QMK_QUANTUM_PATH}/keymap_common.c
  ${QMK_QUANTUM_PATH}/keymap_introspection.c
  ${QMK_QUANTUM_PATH}/eeconfig.c
  ${QMK_QUANTUM_PATH}/color.c
  ${QMK_QUANTUM_PATH}/mousekey.c
  ${QMK_QUANTUM_PATH}/process_keycode/process_default_layer.c
  ${QMK_QUANTUM_PATH}/process_keycode/process_oneshot.c
  ${QMK_QUANTUM_PATH}/process_keycode/process_quantum.c

  ${QMK_QUANTUM_PATH}/logging/*.c
  ${QMK_QUANTUM_PATH}/send_string/*.c
  ${QMK_QUANTUM_PATH}/process_keycode/process_grave_esc.c
)

set(QMK_INC_DIR
  # vial.c 가 vial_generated_keyboard_definition.h 를 찾는 자리
  ${QMK_BOARD_PATH}

  ${CMAKE_CURRENT_SOURCE_DIR}/ap/modules/link
  ${CMAKE_CURRENT_SOURCE_DIR}/ap/modules/qmk

  # "quantum/led.h" 처럼 디렉토리를 붙여 부를 수 있게 upstream 루트도 넣는다.
  # led.h 이름 충돌 우회에 필요하다 (port/protocol/host.h 주석 참고).
  ${QMK_UPSTREAM_PATH}

  ${QMK_PORT_PATH}
  ${QMK_PORT_PATH}/port
  ${QMK_PORT_PATH}/port/platforms

  ${QMK_UPSTREAM_PATH}/tmk_core/protocol
  ${QMK_QUANTUM_PATH}/nvm

  ${QMK_QUANTUM_PATH}
  ${QMK_QUANTUM_PATH}/logging
  ${QMK_QUANTUM_PATH}/send_string
  ${QMK_QUANTUM_PATH}/process_keycode
  ${QMK_QUANTUM_PATH}/keymap_extras
  ${QMK_QUANTUM_PATH}/sequencer
)

set(QMK_DEFINES
  QMK_ENABLE

  # VIA
  #   RAW_ENABLE            raw HID 엔드포인트 (IF2, usage page 0xFF60)
  #   DYNAMIC_KEYMAP_ENABLE 키맵을 EEPROM 에 두고 런타임에 고친다
  #   VIA_ENABLE            VIA 프로토콜 (raw_hid_receive 를 via.c 가 구현한다)
  #
  # ★ RAW_ENABLE 은 host_driver_t 에 6번째 멤버를 만든다.
  #   port/driver_usb.c 의 초기화도 같은 #ifdef 로 묶여 있다.
  RAW_ENABLE
  DYNAMIC_KEYMAP_ENABLE
  VIA_ENABLE

  # Vial 은 VIA 위에 얹힌다. VIA_ENABLE 이 같이 켜져 있어야 한다.
  VIAL_ENABLE
  EXTRAKEY_ENABLE
  # MOUSE_ENABLE    : 마우스 리포트 자체 (패스스루 · 마우스키 공통)
  # MOUSEKEY_ENABLE : 키로 마우스를 조작하는 기능
  MOUSE_ENABLE
  MOUSEKEY_ENABLE
  NKRO_ENABLE
  GRAVE_ESC_ENABLE

  QMK_KEYBOARD_H="quantum.h"

  # ★ 트리의 config.h 가 공용 config.h 를 include 한다.
  #   Vial 고유 설정(UID · unlock 조합)만 트리 쪽에 둔다.
  QMK_KEYMAP_CONFIG_H="${QMK_PORT_PATH}/config.h"
  QMK_BOARD_CONFIG_H="${QMK_BOARD_PATH}/config.h"
  KEYMAP_C="${QMK_BOARD_PATH}/keymap.c"
)


# ★ led.h 이름 충돌
#
# upstream 의 tmk_core/protocol/host.h 는 led_t 를 쓰고, 그건 quantum/led.h 에 있다.
# 그런데 우리 include 경로에는 common/hw/include/led.h (baram 의 ledOn/ledOff)가
# 먼저 있어서 QMK 소스가 그걸 집는다.
#
# 타깃 전체의 순서를 바꾸면 반대로 우리 코드가 QMK 의 led.h 를 집는다.
# include 경로 순서로는 풀 수 없어서, QMK 소스에만 quantum/led.h 를 강제로 먼저 넣는다.
# 그 헤더는 <stdint.h> / <stdbool.h> 만 쓰는 독립 헤더라 안전하다.
# (이후 "led.h" 가 baram 것으로 잡혀도 led_t 는 이미 정의된 뒤다)
set_source_files_properties(${QMK_SRC_FILES} PROPERTIES
  COMPILE_OPTIONS "-include;${QMK_QUANTUM_PATH}/led.h")
