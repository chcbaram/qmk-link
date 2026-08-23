# 하위폴더까지 포함한다.
#
file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
  ap/*.c
  ap/*.cpp
  bsp/*.c
  common/*.c
  hw/*.c
)

# ap/modules 아래(QMK · link)는 모듈의 CMakeLists 가 정한다.
# 여기서 같이 긁으면 중복 컴파일되고, QMK 소스에만 주는 컴파일 옵션도 못 받는다.
list(FILTER SRC_FILES EXCLUDE REGEX "/ap/modules/")

include(ap/modules/qmk/${KEY_PROTOCOL}/CMakeLists.txt)

add_executable(${PRJ_NAME}
  main.c

  ${SRC_FILES}
  ${QMK_SRC_FILES}
)

target_compile_definitions(${PRJ_NAME} PRIVATE ${QMK_DEFINES})

# ★ 어느 트리로 구웠는지 이름에 드러낸다.
#   USB 제품 이름과 앱에 뜨는 이름이 "QMK-LINK VIA" / "QMK-LINK VIAL" 로 갈린다.
#   두 펌웨어를 번갈아 구울 때 지금 뭐가 올라가 있는지 바로 보인다.
string(TOUPPER ${KEY_PROTOCOL} KEY_PROTOCOL_UPPER)
target_compile_definitions(${PRJ_NAME} PRIVATE
  KEY_PROTOCOL_NAME="${KEY_PROTOCOL_UPPER}")

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
  hw/driver/usb
  hw/driver/usbh
  hw/driver/usbh/usbh_hid

  ${QMK_INC_DIR}
)

target_link_libraries(${PRJ_NAME}
  pico_stdlib
  hardware_pio
  hardware_flash
  pico_flash
  pico_multicore
  pico_unique_id
  tinyusb_device
  tinyusb_host
  tinyusb_pico_pio_usb
  pico_usb_reset

  # Key2(Reset) 더블클릭 -> BOOTSEL.
  # main() 전에 도는 생성자가 알아서 처리한다. 우리 코드는 없다.
  pico_bootsel_via_double_reset
)

# BOOTSEL 재부팅 경로 두 가지 (pico_usb_reset 라이브러리가 처리한다)
#   VIA_BAUD_RATE        : CDC 1200bps touch. 1순위. flash.py 가 쓴다
#   VIA_VENDOR_INTERFACE : picotool 호환. libusb 를 타서 Windows 에선 드라이버가 필요하다
target_compile_definitions(${PRJ_NAME} PRIVATE
  PICO_ENABLE_USB_RESET_VIA_BAUD_RATE=1
  PICO_ENABLE_USB_RESET_VIA_VENDOR_INTERFACE=1
  PICO_USB_RESET_INCLUDE_DEFAULT_APP_DRIVER_CB=1
  PICO_USB_RESET_SUPPORT_MS_OS_20_DESCRIPTOR=0

  # 더블클릭 인정 시간. 이만큼 부팅이 늦어진다 (SDK 기본값 200ms).
  # RP2350 은 POWMAN_CHIP_RESET 의 DOUBLE_TAP 비트를 쓴다.
  # RUN 리셋이 SRAM 을 지우기 때문에 (실측 확인) RAM 매직은 쓸 수 없다.
  PICO_BOOTSEL_VIA_DOUBLE_RESET_TIMEOUT_MS=500
)

pico_generate_pio_header(${PRJ_NAME} ${CMAKE_CURRENT_LIST_DIR}/hw/driver/pio/ws2812.pio)

# ★ 펌웨어를 통째로 RAM 에서 실행한다.
#
#   플래시를 소거·기록하는 동안에는 XIP 가 멈춘다. 그때 core1 이 PIO USB 를
#   돌고 있으면 코드를 인출하지 못해 죽으므로, 원래는 flash_safe_execute() 로
#   core1 을 세워야 했다. 그런데 그 정지(소거 34ms)가 SOF 를 끊어 키보드를
#   서스펜드에 빠뜨렸고, 뒤처리(재열거)로 되살리는 것도 안정적이지 않았다.
#
#   RAM 에서 돌면 애초에 정지시킬 이유가 없다. RP2350 은 SRAM 520KB 고
#   펌웨어는 110KB 라 여유가 충분하다.
#
#   PICO_FLASH_ASSUME_CORE1_SAFE : core1 은 플래시를 건드리지 않는다고 알린다.
#   그래야 flash_safe_execute() 가 core1 을 세우지 않는다.
pico_set_binary_type(${PRJ_NAME} copy_to_ram)
target_compile_definitions(${PRJ_NAME} PRIVATE PICO_FLASH_ASSUME_CORE1_SAFE=1)

pico_add_extra_outputs(${PRJ_NAME})

# 로그는 2단계에서 자체 USB CDC 로 붙인다.
# pico_stdio_usb 는 자체 descriptor 를 갖고 있어 HID 와 공존이 안 되므로 쓰지 않는다.
#
pico_enable_stdio_usb(${PRJ_NAME}  0)
pico_enable_stdio_uart(${PRJ_NAME} 0)

set_target_properties(${PRJ_NAME} PROPERTIES SUFFIX ".elf")

