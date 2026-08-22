# 하위폴더까지 포함한다.
#
file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
  ap/*.c
  ap/*.cpp
  bsp/*.c
  common/*.c
  hw/*.c
)

add_executable(${PRJ_NAME}
  main.c

  ${SRC_FILES}
)

target_include_directories(${PRJ_NAME} PUBLIC
  .
  ap
  ap/modules
  bsp
  common
  common/core
  common/hw/include
  hw
  hw/driver
)

target_link_libraries(${PRJ_NAME}
  pico_stdlib
  hardware_pio
  pico_multicore
)

pico_generate_pio_header(${PRJ_NAME} ${CMAKE_CURRENT_LIST_DIR}/hw/driver/pio/ws2812.pio)

pico_add_extra_outputs(${PRJ_NAME})

# 로그는 2단계에서 자체 USB CDC 로 붙인다.
# pico_stdio_usb 는 자체 descriptor 를 갖고 있어 HID 와 공존이 안 되므로 쓰지 않는다.
#
pico_enable_stdio_usb(${PRJ_NAME}  0)
pico_enable_stdio_uart(${PRJ_NAME} 0)

set_target_properties(${PRJ_NAME} PROPERTIES SUFFIX ".elf")
