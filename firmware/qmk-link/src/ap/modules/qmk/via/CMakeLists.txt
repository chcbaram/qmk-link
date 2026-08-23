# QMK (VIA 트리)
#
# quantum 은 upstream 을 그대로 쓴다 (firm-sdk/upstream/qmk_firmware).
# 우리 저장소에는 port 레이어와 키보드 정의만 있다.

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

  ${QMK_PORT_PATH}/port/*.c
  ${QMK_PORT_PATH}/port/platforms/*.c
  ${QMK_PORT_PATH}/port/protocol/*.c

  ${QMK_QUANTUM_PATH}/quantum.c
  ${QMK_QUANTUM_PATH}/via.c
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
  ${QMK_QUANTUM_PATH}/dynamic_keymap.c
  ${QMK_QUANTUM_PATH}/color.c

  ${QMK_QUANTUM_PATH}/logging/*.c
  ${QMK_QUANTUM_PATH}/send_string/*.c
  ${QMK_QUANTUM_PATH}/process_keycode/process_grave_esc.c
)

set(QMK_INC_DIR
  ${CMAKE_CURRENT_SOURCE_DIR}/ap/modules/link

  # "quantum/led.h" 처럼 디렉토리를 붙여 부를 수 있게 upstream 루트도 넣는다.
  # led.h 이름 충돌 우회에 필요하다 (port/protocol/host.h 주석 참고).
  ${QMK_UPSTREAM_PATH}

  ${QMK_PORT_PATH}
  ${QMK_PORT_PATH}/port
  ${QMK_PORT_PATH}/port/platforms
  ${QMK_PORT_PATH}/port/protocol

  ${QMK_QUANTUM_PATH}
  ${QMK_QUANTUM_PATH}/logging
  ${QMK_QUANTUM_PATH}/send_string
  ${QMK_QUANTUM_PATH}/process_keycode
  ${QMK_QUANTUM_PATH}/keymap_extras
  ${QMK_QUANTUM_PATH}/sequencer
)

set(QMK_DEFINES
  VIA_ENABLE
  RAW_ENABLE
  DYNAMIC_KEYMAP_ENABLE
  EXTRAKEY_ENABLE
  MOUSEKEY_ENABLE
  NKRO_ENABLE
  GRAVE_ESC_ENABLE

  QMK_KEYBOARD_H="quantum.h"
  QMK_KEYMAP_CONFIG_H="${QMK_PORT_PATH}/config.h"
  KEYMAP_C="${QMK_PORT_PATH}/keymap.c"
)
