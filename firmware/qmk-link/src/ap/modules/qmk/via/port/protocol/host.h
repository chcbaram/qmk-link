#pragma once

#include "hw_def.h"
#include "cli.h"

#include QMK_KEYMAP_CONFIG_H

#include "host_driver.h"
// ★ QMK 의 led.h 를 여기서 먼저 끌어들인다.
//
// quantum.h 는 50줄에서 host.h, 51줄에서 "led.h" 를 include 한다.
// 그런데 우리 include 경로에는 common/hw/include/led.h (baram 의 ledOn/ledOff)가
// 먼저 있어서 51줄이 그걸 집는다. QMK 가 기대하는 led_t / led_set() 이 없어 깨진다.
//
// 여기서 경로를 명시해 먼저 넣어 두면, 51줄에서 다른 led.h 가 잡혀도
// led_t 는 이미 정의된 뒤라 문제가 없다. hola-mini / wish-he 도 같은 우회를 쓴다.
#include "quantum/led.h"


/* host driver */
void           host_set_driver(host_driver_t *driver);
host_driver_t *host_get_driver(void);


/* host driver interface */
uint8_t host_keyboard_leds(void);
led_t   host_keyboard_led_state(void);
void    host_keyboard_send(report_keyboard_t *report);
void    host_nkro_send(report_nkro_t *report);
void    host_mouse_send(report_mouse_t *report);
void    host_system_send(uint16_t usage);
void    host_consumer_send(uint16_t usage);
void    host_programmable_button_send(uint32_t data);

uint16_t host_last_system_usage(void);
uint16_t host_last_consumer_usage(void);

/* NKRO 가능 여부 (report protocol 일 때만). action_util.c 의 send_keyboard_report 가 참조. */
bool host_can_send_nkro(void);

/*
 * 현재 USB 키보드 프로토콜 (1=report/NKRO 가능, 0=boot).
 * stock report.c/action_util.c 가 이 이름을 "변수처럼" 참조하므로(그 파일들은 건드리지
 * 않는다), 가변 전역을 노출하는 대신 접근 함수를 매크로로 연결한다. USB 상태를 실시간으로
 * 읽어 별도 동기화가 필요 없다. 리포트 조립 시에만 호출되어 핫패스가 아니다.
 */
uint8_t keyboard_protocol_get(void);
#define keyboard_protocol (keyboard_protocol_get())