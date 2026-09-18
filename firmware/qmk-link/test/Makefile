# 호스트에서 도는 시뮬레이션.
#
# ★ 펌웨어 소스를 그대로 컴파일한다. 베껴 온 사본이 아니다.
#   흉내내는 것은 TinyUSB 와 시간뿐이다 (stub/).
#
#   make          지금 소스로 돌린다
#   make rev REV=269f452   그 커밋의 소스로 돌린다 (회귀 재현용)
#   make clean

CC      ?= cc
CFLAGS  := -std=c11 -g -Wall -Wno-macro-redefined
SRC_DIR := ../src
HW_DIR  := $(SRC_DIR)/hw
USB_DIR := $(HW_DIR)/driver/usb
BUILD   := build

TESTS   := hid_queue_sim

.PHONY: all run clean rev
all: run

run: $(addprefix $(BUILD)/,$(TESTS))
	@for t in $^; do ./$$t || exit 1; done

$(BUILD)/hid_queue_sim: hid_queue_sim.c $(USB_DIR)/usbd_hid.c $(USB_DIR)/usbd_hid.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -DSIM_NAME='"지금 소스"' \
	  -Istub -I$(HW_DIR) -I$(USB_DIR) \
	  hid_queue_sim.c $(USB_DIR)/usbd_hid.c -o $@

# 옛 커밋의 usbd_hid 로 돌린다. 버그가 재현되는지 보는 용도다.
#   make rev REV=269f452   -> 리포트 큐 넣기 전. 1~4 번이 실패해야 정상이다
rev:
	@test -n "$(REV)" || (echo "REV= 를 줘야 한다. 예: make rev REV=269f452"; exit 1)
	@mkdir -p $(BUILD)/$(REV)
	git show $(REV):firmware/qmk-link/src/hw/driver/usb/usbd_hid.c > $(BUILD)/$(REV)/usbd_hid.c
	git show $(REV):firmware/qmk-link/src/hw/driver/usb/usbd_hid.h > $(BUILD)/$(REV)/usbd_hid.h
	$(CC) $(CFLAGS) -w -DSIM_NAME='"$(REV) 소스"' \
	  -Istub -I$(HW_DIR) -I$(BUILD)/$(REV) \
	  hid_queue_sim.c $(BUILD)/$(REV)/usbd_hid.c -o $(BUILD)/sim_$(REV)
	-./$(BUILD)/sim_$(REV)

clean:
	rm -rf $(BUILD)
