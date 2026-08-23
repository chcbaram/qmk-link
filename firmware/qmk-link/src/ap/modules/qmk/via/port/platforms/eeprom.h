#pragma once

// QMK 의 EEPROM API. upstream platforms/eeprom.h 와 시그니처가 같아야 한다.
// 구현은 port/platforms/eeprom.c — RAM 섀도 + 플래시 지연 플러시.

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

uint8_t  eeprom_read_byte(const uint8_t *__p);
uint16_t eeprom_read_word(const uint16_t *__p);
uint32_t eeprom_read_dword(const uint32_t *__p);
void     eeprom_read_block(void *__dst, const void *__src, size_t __n);
void     eeprom_write_byte(uint8_t *__p, uint8_t __value);
void     eeprom_write_word(uint16_t *__p, uint16_t __value);
void     eeprom_write_dword(uint32_t *__p, uint32_t __value);
void     eeprom_write_block(const void *__src, void *__dst, size_t __n);
void     eeprom_update_byte(uint8_t *__p, uint8_t __value);
void     eeprom_update_word(uint16_t *__p, uint16_t __value);
void     eeprom_update_dword(uint32_t *__p, uint32_t __value);
void     eeprom_update_block(const void *__src, void *__dst, size_t __n);

void     eeprom_driver_init(void);
void     eeprom_driver_format(bool erase);
void     eeprom_driver_erase(void);

// ★ 계속 불려야 한다 — qmk.c 의 qmkUpdate() 가 부른다.
//   조용해진 dirty 섹터를 하나씩 플래시에 쓴다. 안 부르면 전원 내릴 때 다 날아간다.
void     eeprom_task(void);

// 리셋 / BOOTSEL 진입 직전에 미저장분을 동기 기록한다.
void     eeprom_flush(void);

// 진단용
bool     eepromIsInit(void);      // eeprom_driver_init() 전에는 섀도가 비어 있다
uint32_t eepromGetFlushCount(void);
uint32_t eepromGetFlushTime(void);
uint32_t eepromGetDirtyMask(void);
