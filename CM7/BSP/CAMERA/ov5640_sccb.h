/**
 ****************************************************************************************************
 * @file        ov5640_sccb.h
 * @brief       OV5640模块SCCB接口驱动代码（软件模拟，PB6=SCL, PB7=SDA）
 ****************************************************************************************************
 */

#ifndef __OV5640_SCCB_H
#define __OV5640_SCCB_H

#include "bsp_sys.h"

/* SCCB软件时序IO操作（SCL/SDA由GPIO直接控制）。 */
#define OV5640_SCCB_SCL(x)                  do{ x ?                                                             \
                                                    HAL_GPIO_WritePin(OV5640_SCL_GPIO_Port, OV5640_SCL_Pin, GPIO_PIN_SET) :  \
                                                    HAL_GPIO_WritePin(OV5640_SCL_GPIO_Port, OV5640_SCL_Pin, GPIO_PIN_RESET); \
                                                }while(0)
#define OV5640_SCCB_SDA(x)                  do{ x ?                                                             \
                                                    HAL_GPIO_WritePin(OV5640_SDA_GPIO_Port, OV5640_SDA_Pin, GPIO_PIN_SET) :  \
                                                    HAL_GPIO_WritePin(OV5640_SDA_GPIO_Port, OV5640_SDA_Pin, GPIO_PIN_RESET); \
                                                }while(0)
#define OV5640_SCCB_READ_SDA()              HAL_GPIO_ReadPin(OV5640_SDA_GPIO_Port, OV5640_SDA_Pin)

/* SCCB总线初始化及寄存器读写操作。 */
void ov5640_sccb_init(void);                                                        /* 将总线恢复为空闲状态 */
void ov5640_sccb_3_phase_write(uint8_t id_addr, uint16_t sub_addr, uint8_t dat);   /* 写寄存器 */
void ov5640_sccb_2_phase_write(uint8_t id_addr, uint16_t sub_addr);                 /* 写寄存器地址 */
void ov5640_sccb_2_phase_read(uint8_t id_addr, uint8_t *dat);                       /* 读寄存器数据 */

#endif
