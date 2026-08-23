/*
 * port/platforms/eeprom.c — QMK 의 EEPROM 백엔드
 *
 * ★ 05단계는 RAM 으로 흉내만 낸다. 전원을 내리면 사라진다.
 *
 *   flash 로 옮기는 것은 06단계(VIA)다. 그때 진짜 문제가 있다 —
 *   flash 를 지우거나 쓰는 동안 XIP 가 멈추는데 core1 이 PIO USB 를 돌고 있다.
 *   flash_safe_execute() + multicore_lockout_victim_init() 로 core1 을 잠가야 하고,
 *   그 사이 USB 호스트와 device 가 둘 다 멈춘다. 그 실험이 06단계의 첫 항목이다.
 *
 *   05단계는 키 처리만 확인하면 되고 저장이 필요 없다. 여기서 붙들리지 않는다.
 */

#include "eeprom.h"
#include <string.h>


#ifndef TOTAL_EEPROM_BYTE_COUNT
#define TOTAL_EEPROM_BYTE_COUNT 4096
#endif


static uint8_t eeprom_buf[TOTAL_EEPROM_BYTE_COUNT];


void eeprom_driver_init(void)
{
  memset(eeprom_buf, 0xFF, sizeof(eeprom_buf));
}

void eeprom_driver_format(bool erase)
{
  (void)erase;
  memset(eeprom_buf, 0xFF, sizeof(eeprom_buf));
}

void eeprom_driver_erase(void)
{
  memset(eeprom_buf, 0xFF, sizeof(eeprom_buf));
}

void eeprom_read_block(void *buf, const void *addr, size_t len)
{
  uintptr_t offset = (uintptr_t)addr;

  if (offset >= sizeof(eeprom_buf)) return;
  if (offset + len > sizeof(eeprom_buf)) len = sizeof(eeprom_buf) - offset;

  memcpy(buf, &eeprom_buf[offset], len);
}

void eeprom_write_block(const void *buf, void *addr, size_t len)
{
  uintptr_t offset = (uintptr_t)addr;

  if (offset >= sizeof(eeprom_buf)) return;
  if (offset + len > sizeof(eeprom_buf)) len = sizeof(eeprom_buf) - offset;

  memcpy(&eeprom_buf[offset], buf, len);
}

uint8_t eeprom_read_byte(const uint8_t *addr)
{
  uint8_t data = 0xFF;
  eeprom_read_block(&data, addr, 1);
  return data;
}

void eeprom_write_byte(uint8_t *addr, uint8_t value)
{
  eeprom_write_block(&value, addr, 1);
}

uint16_t eeprom_read_word(const uint16_t *addr)
{
  uint16_t data = 0xFFFF;
  eeprom_read_block(&data, addr, 2);
  return data;
}

void eeprom_write_word(uint16_t *addr, uint16_t value)
{
  eeprom_write_block(&value, addr, 2);
}

uint32_t eeprom_read_dword(const uint32_t *addr)
{
  uint32_t data = 0xFFFFFFFF;
  eeprom_read_block(&data, addr, 4);
  return data;
}

void eeprom_write_dword(uint32_t *addr, uint32_t value)
{
  eeprom_write_block(&value, addr, 4);
}

void eeprom_update_block(const void *buf, void *addr, size_t len)
{
  eeprom_write_block(buf, addr, len);
}

void eeprom_update_byte(uint8_t *addr, uint8_t value)   { eeprom_write_byte(addr, value); }
void eeprom_update_word(uint16_t *addr, uint16_t value) { eeprom_write_word(addr, value); }
void eeprom_update_dword(uint32_t *addr, uint32_t value){ eeprom_write_dword(addr, value); }

void eeprom_flush(void)
{
  // RAM 백엔드라 미저장분이 없다.
  // 06단계에서 flash 로 바꾸면 여기서 실제로 써야 한다 (wish-he 와 같은 이유).
}
