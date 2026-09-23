/**
 ****************************************************************************************************
 * @file        ov5640_sccb.h
 * @brief       OV5640模块SCCB接口驱动代码（软件模拟，PB6=SCL, PB7=SDA）
 ****************************************************************************************************
 */

#ifndef __OV5640_SCCB_H
#define __OV5640_SCCB_H

#include "bsp_sys.h"

/* IO操作 */
#define OV5640_SCCB_SCL(x)                  do{ x ?                                                             \
                                                    HAL_GPIO_WritePin(OV5640_SCL_GPIO_Port, OV5640_SCL_Pin, GPIO_PIN_SET) :  \
                                                    HAL_GPIO_WritePin(OV5640_SCL_GPIO_Port, OV5640_SCL_Pin, GPIO_PIN_RESET); \
                                                }while(0)
#define OV5640_SCCB_SDA(x)                  do{ x ?                                                             \
                                                    HAL_GPIO_WritePin(OV5640_SDA_GPIO_Port, OV5640_SDA_Pin, GPIO_PIN_SET) :  \
                                                    HAL_GPIO_WritePin(OV5640_SDA_GPIO_Port, OV5640_SDA_Pin, GPIO_PIN_RESET); \
                                                }while(0)
#define OV5640_SCCB_READ_SDA()              HAL_GPIO_ReadPin(OV5640_SDA_GPIO_Port, OV5640_SDA_Pin)

/* 操作函数 */
void ov5640_sccb_init(void);
void ov5640_sccb_3_phase_write(uint8_t id_addr, uint16_t sub_addr, uint8_t dat);
void ov5640_sccb_2_phase_write(uint8_t id_addr, uint16_t sub_addr);
void ov5640_sccb_2_phase_read(uint8_t id_addr, uint8_t *dat);

#endif
