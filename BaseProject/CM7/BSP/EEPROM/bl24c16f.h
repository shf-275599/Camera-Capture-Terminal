/**
  ******************************************************************************
  * @file    bl24c16f.h
  * @brief   BL24C16F I2C EEPROM 驱动头文件
  *
  * 芯片说明：
  *   BL24C16F 为 16Kbit（2048 字节）I2C 接口 EEPROM，兼容 AT24C16/M24C16 寻址方式。
  *
  * 寻址方式：
  *   内部地址共 11 位（A10~A0），其中高 3 位 A10/A9/A8 编入 I2C 设备地址，
  *   低 8 位 A7~A0 作为字地址单独发送。
  *   设备地址格式：1010 A10 A9 A8 R/W
  *   8 位写地址范围：0xA0 ~ 0xAE（共 8 个地址块，每块 256 字节）
  *
  * 使用示例：
  *   BL24C16F_HandleTypeDef eeprom;
  *   BL24C16F_Init(&eeprom, &hi2c4);
  *   BL24C16F_Write(&eeprom, 0x0000, buf, 4);
  *   BL24C16F_Read(&eeprom, 0x0000, buf, 4);
  ******************************************************************************
  */

#ifndef BL24C16F_H
#define BL24C16F_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* -------------------------------------------------------------------------- */
/*                              芯片参数宏定义                                 */
/* -------------------------------------------------------------------------- */

/** @brief 总容量：2048 字节（2 KByte） */
#define BL24C16F_TOTAL_SIZE       (2048U)

/** @brief 页写入大小：16 字节（单次页写不可跨页） */
#define BL24C16F_PAGE_SIZE        (16U)

/** @brief 地址块大小：256 字节（A10/A9/A8 每变化 1 对应一块） */
#define BL24C16F_BLOCK_SIZE       (256U)

/** @brief 最大有效地址：0x7FF */
#define BL24C16F_MAX_ADDR         (BL24C16F_TOTAL_SIZE - 1U)

/**
 * @brief 基础 8 位写地址（A10=A9=A8=0 时）
 *        对应 7 位从机地址 0x50，存储范围 0x000~0x0FF
 */
#define BL24C16F_DEV_ADDR_BASE    (0xA0U)

/** @brief 内部写周期时间（ms），手册典型值 5ms */
#define BL24C16F_WRITE_CYCLE_MS   (5U)

/** @brief I2C 通信默认超时时间（ms） */
#define BL24C16F_DEFAULT_TIMEOUT  (100U)

/* -------------------------------------------------------------------------- */
/*                              驱动数据结构                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief BL24C16F 驱动句柄
 */
typedef struct
{
  I2C_HandleTypeDef *hi2c;   /**< 绑定的 I2C 外设句柄，如 &hi2c4 */
  uint32_t timeout;          /**< I2C 操作超时时间（ms） */
} BL24C16F_HandleTypeDef;

/* -------------------------------------------------------------------------- */
/*                              对外接口函数                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief  初始化 EEPROM 驱动句柄
 * @param  hdev  驱动句柄指针
 * @param  hi2c  已初始化的 I2C 外设句柄
 */
void BL24C16F_Init(BL24C16F_HandleTypeDef *hdev, I2C_HandleTypeDef *hi2c);

/**
 * @brief  检测 EEPROM 是否就绪（应答检测）
 * @param  hdev  驱动句柄指针
 * @retval HAL_OK     设备应答正常
 * @retval HAL_ERROR  参数无效或设备无应答
 * @retval HAL_TIMEOUT 超时
 */
HAL_StatusTypeDef BL24C16F_IsReady(BL24C16F_HandleTypeDef *hdev);

/**
 * @brief  从指定地址读取多个字节
 * @param  hdev  驱动句柄指针
 * @param  addr  起始存储地址（0x0000 ~ 0x07FF）
 * @param  data  接收数据缓冲区
 * @param  len   读取字节数
 * @retval HAL_OK     读取成功
 * @retval HAL_ERROR  参数错误或地址越界
 * @retval HAL_TIMEOUT I2C 超时
 */
HAL_StatusTypeDef BL24C16F_Read(BL24C16F_HandleTypeDef *hdev, uint16_t addr, uint8_t *data, uint16_t len);

/**
 * @brief  向指定地址写入多个字节（自动按页拆分并等待写周期）
 * @param  hdev  驱动句柄指针
 * @param  addr  起始存储地址（0x0000 ~ 0x07FF）
 * @param  data  待写入数据缓冲区
 * @param  len   写入字节数
 * @retval HAL_OK     写入成功
 * @retval HAL_ERROR  参数错误或地址越界
 * @retval HAL_TIMEOUT I2C 超时或写周期等待超时
 */
HAL_StatusTypeDef BL24C16F_Write(BL24C16F_HandleTypeDef *hdev, uint16_t addr, const uint8_t *data, uint16_t len);

/**
 * @brief  向指定地址写入单字节
 * @param  hdev  驱动句柄指针
 * @param  addr  存储地址
 * @param  data  待写入的字节
 */
HAL_StatusTypeDef BL24C16F_WriteByte(BL24C16F_HandleTypeDef *hdev, uint16_t addr, uint8_t data);

/**
 * @brief  从指定地址读取单字节
 * @param  hdev  驱动句柄指针
 * @param  addr  存储地址
 * @param  data  接收字节的指针
 */
HAL_StatusTypeDef BL24C16F_ReadByte(BL24C16F_HandleTypeDef *hdev, uint16_t addr, uint8_t *data);

#ifdef __cplusplus
}
#endif

#endif /* BL24C16F_H */
