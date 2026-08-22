cmake_minimum_required(VERSION 3.13)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")

# 프로젝트 이름은 최종 출력 파일 이름으로 사용된다.
#
set(PRJ_NAME qmk-link)

# 키 프로토콜 트리 선택 (5~7단계에서 사용한다)
#   via  : qmk_firmware  기반
#   vial : vial-qmk      기반
#
set(KEY_PROTOCOL "via" CACHE STRING "key protocol tree: via | vial")

# SDK 는 firm-sdk 서브모듈을 쓴다. 환경변수 PICO_SDK_PATH 에 의존하지 않는다.
#
if (NOT DEFINED PICO_SDK_PATH)
  set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/../firm-sdk/pico-sdk)
endif()

# picotool 을 SDK 가 직접 빌드할 때 받는 위치.
# build/ 밖에 둬서 clean 빌드마다 다시 받지 않게 한다.
#
if (NOT DEFINED PICOTOOL_FETCH_FROM_GIT_PATH)
  set(PICOTOOL_FETCH_FROM_GIT_PATH ${CMAKE_CURRENT_LIST_DIR}/../firm-sdk/.picotool)
endif()

# 커스텀 보드 헤더.
# PICO_BOARD_HEADER_DIRS 는 pico_sdk_init() 안의 generic_board.cmake 가 읽으므로
# 반드시 pico_sdk_init() 이전에 설정해야 한다.
#
# Pico-PIO-USB (USB-A 호스트 포트).
# pico_sdk_init() 안에서 tinyusb 의 family.cmake 가 이 경로를 보고
# tinyusb_pico_pio_usb 타깃과 hcd_pio_usb.c 를 자동으로 엮는다.
# 그래서 pico_sdk_init() 이전에 지정해야 한다.
if (NOT DEFINED PICO_PIO_USB_PATH)
  set(PICO_PIO_USB_PATH ${CMAKE_CURRENT_LIST_DIR}/../firm-sdk/Pico-PIO-USB)
endif()

set(PICO_BOARD qmk_link)
list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/src/bsp/board)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(${PRJ_NAME}
  LANGUAGES C CXX ASM
)

pico_sdk_init()

add_compile_options(
  -Wall
  -Wno-format             # int != int32_t as far as the compiler is concerned
  -Wno-unused-function    # 문서용으로만 있는 함수가 있다
  -Wno-maybe-uninitialized
)

add_link_options(
  -Xlinker -print-memory-usage -Xlinker
)

add_subdirectory(src)
