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
#include "mylog.h"
#include <string.h>

#define BSP_EEPROM_USER_BASE (0x0200U)  // 应用可用区（自检/业务参数）
static BL24C16F_HandleTypeDef s_eeprom; // EEPROM 实例句柄

/**
 * @brief   初始化 EEPROM 实例
 * @return  0 成功，-1 失败
 * @note    初始化 EEPROM 实例，检查是否就绪
 */
int bsp_eeprom_init(void)
{
  HAL_StatusTypeDef status; // HAL 状态码

  BL24C16F_Init(&s_eeprom, &hi2c4); // 初始化 EEPROM 实例

  status = BL24C16F_IsReady(&s_eeprom); // 探测器件 ACK，确认芯片在线
  if (status != HAL_OK)                 // 无应答：器件缺失或 I2C 总线故障
  {
    LOG_ERROR("BOOT", "EEPROM not ready, status=%d", (int)status); // 打印失败状态码
    return -1;                                                     // 返回失败，上层跳过全部 EEPROM 操作
  }

  LOG_INFO("BOOT", "EEPROM ready (BL24C16F @ I2C4)"); // 就绪提示
  return 0;                                           // 返回成功，允许上层读写元数据
}

BL24C16F_HandleTypeDef *bsp_eeprom_handle(void)
{
  return &s_eeprom; // 返回全局句柄，供 boot_eeprom 层拷贝使用
}

int bsp_eeprom_selftest(void)
{
  uint8_t write_buf[4] = {0x11, 0x22, 0x33, 0x44}; // 自检写入的特征数据
  uint8_t read_buf[4] = {0};                       // 读回缓冲区，清零待比对
  HAL_StatusTypeDef status;                        // HAL 状态码

  if (s_eeprom.hi2c == NULL) // 句柄未绑定 I2C：尚未初始化
  {
    LOG_ERROR("BOOT", "EEPROM selftest: not inited"); // 提示先调 bsp_eeprom_init
    return -1;                                        // 未初始化直接失败
  }

  status = BL24C16F_Write(&s_eeprom, BSP_EEPROM_USER_BASE, write_buf, sizeof(write_buf)); // 向用户区写 4 字节
  if (status != HAL_OK)                                                                   // 写失败：总线或器件异常
  {
    LOG_ERROR("BOOT", "EEPROM write failed, status=%d", (int)status); // 打印写失败状态
    return -1;                                                        // 写失败即中止自检
  }

  status = BL24C16F_Read(&s_eeprom, BSP_EEPROM_USER_BASE, read_buf, sizeof(read_buf)); // 从同一地址读回 4 字节
  if (status != HAL_OK)                                                                // 读失败：总线或器件异常
  {
    LOG_ERROR("BOOT", "EEPROM read failed, status=%d", (int)status); // 打印读失败状态
    return -1;                                                       // 读失败即中止自检
  }

  if (memcmp(write_buf, read_buf, sizeof(read_buf)) != 0) // 写入值与读回值不一致
  {
    LOG_ERROR("BOOT", "EEPROM data mismatch"); // 提示存储内容异常
    return -1;                                 // 比对失败
  }

  LOG_INFO("BOOT", "EEPROM read/write OK @ 0x%04X", (unsigned int)BSP_EEPROM_USER_BASE); // 打印自检通过及测试地址
  return 0;                                                                              // 全部通过
}
