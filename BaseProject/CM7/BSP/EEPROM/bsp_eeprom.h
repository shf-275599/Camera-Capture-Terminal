/**
 * @file    bsp_eeprom.h
 * @brief   板载 BL24C16F EEPROM 封装（挂在 I2C4：PD12/PD13）
 */
#ifndef BSP_EEPROM_H
#define BSP_EEPROM_H

#include "bl24c16f.h"

/**
 * @brief  绑定 I2C4 并探测 EEPROM 是否应答
 * @retval 0 就绪，-1 无应答或参数错误
 */
int bsp_eeprom_init(void);

/**
 * @brief  获取全局 EEPROM 句柄（须先 bsp_eeprom_init）
 */
BL24C16F_HandleTypeDef *bsp_eeprom_handle(void);

/**
 * @brief  简单读写自检（写 4 字节再读回比对），结果打印到串口
 * @retval 0 通过，-1 失败
 */
int bsp_eeprom_selftest(void);

#endif /* BSP_EEPROM_H */
