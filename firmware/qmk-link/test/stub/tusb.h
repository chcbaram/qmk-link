#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// 시뮬레이터가 구현한다 (sim_main.c). 실제 TinyUSB 를 흉내낸다.
typedef enum { HID_REPORT_TYPE_INVALID = 0, HID_REPORT_TYPE_INPUT, HID_REPORT_TYPE_OUTPUT, HID_REPORT_TYPE_FEATURE } hid_report_type_t;
#define TUSB_DESC_STRING 3

bool tud_hid_n_ready(uint8_t itf);
bool tud_hid_n_report(uint8_t itf, uint8_t report_id, const void *report, uint16_t len);
bool tud_hid_n_mouse_report(uint8_t itf, uint8_t report_id, uint8_t buttons, int8_t x, int8_t y, int8_t v, int8_t h);
uint8_t tud_hid_n_get_protocol(uint8_t itf);
bool tud_mounted(void);
bool tud_suspended(void);
void tud_task(void);

const uint8_t *usbdHidGetReportDesc(uint8_t itf, uint16_t *p_len);
