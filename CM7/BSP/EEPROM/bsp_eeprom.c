/**
 * @file    bsp_eeprom.c
 * @brief   板载 BL24C16F EEPROM 封装实现
 *
 * EEPROM 布局约定（与 Bootloader boot_eeprom 一致）：
 *   0x0000~0x00FF  Boot meta copy0
 *   0x0100~0x01FF  Boot meta copy1
 *   0x0200~        应用可用区（自检/业务参数）
 */
#include "bsp_eeprom.h"
#include "i2c.h"
#include <stdio.h>
#include <string.h>

/** 避开 Boot 双备份区 */
#define BSP_EEPROM_USER_BASE  (0x0200U)

static BL24C16F_HandleTypeDef s_eeprom;

int bsp_eeprom_init(void)
{
  HAL_StatusTypeDef status;

  BL24C16F_Init(&s_eeprom, &hi2c4);

  status = BL24C16F_IsReady(&s_eeprom);
  if (status != HAL_OK)
  {
    printf("EEPROM not ready, status=%d\r\n", (int)status);
    return -1;
  }

  printf("EEPROM ready (BL24C16F @ I2C4)\r\n");
  return 0;
}

BL24C16F_HandleTypeDef *bsp_eeprom_handle(void)
{
  return &s_eeprom;
}

int bsp_eeprom_selftest(void)
{
  uint8_t write_buf[4] = {0x11, 0x22, 0x33, 0x44};
  uint8_t read_buf[4] = {0};
  HAL_StatusTypeDef status;

  if (s_eeprom.hi2c == NULL)
  {
    printf("EEPROM selftest: not inited\r\n");
    return -1;
  }

  status = BL24C16F_Write(&s_eeprom, BSP_EEPROM_USER_BASE, write_buf, sizeof(write_buf));
  if (status != HAL_OK)
  {
    printf("EEPROM write failed, status=%d\r\n", (int)status);
    return -1;
  }

  status = BL24C16F_Read(&s_eeprom, BSP_EEPROM_USER_BASE, read_buf, sizeof(read_buf));
  if (status != HAL_OK)
  {
    printf("EEPROM read failed, status=%d\r\n", (int)status);
    return -1;
  }

  if (memcmp(write_buf, read_buf, sizeof(read_buf)) != 0)
  {
    printf("EEPROM data mismatch\r\n");
    return -1;
  }

  printf("EEPROM read/write OK @ 0x%04X\r\n", (unsigned int)BSP_EEPROM_USER_BASE);
  return 0;
}
