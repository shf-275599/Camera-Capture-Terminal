/**
 ****************************************************************************************************
 * @file        ov5640_sccb.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2022-06-21
 * @brief       OV5640模块SCCB接口驱动代码
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 阿波罗 H743开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 *
 ****************************************************************************************************
 */

#ifndef __OV5640_SCCB_H
#define __OV5640_SCCB_H

#include "bsp_sys.h"

/* 定义OV5640的SCCB通讯引脚是否有外接上拉电阻 */
#define ov5640_SCCB_GPIO_PULLUP             1

/* 引脚定义 */
#define ov5640_SCCB_SCL_GPIO_PORT           GPIOB
#define ov5640_SCCB_SCL_GPIO_PIN            GPIO_PIN_4
#define ov5640_SCCB_SCL_GPIO_CLK_ENABLE()   do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)
#define ov5640_SCCB_SDA_GPIO_PORT           GPIOB
#define ov5640_SCCB_SDA_GPIO_PIN            GPIO_PIN_3
#define ov5640_SCCB_SDA_GPIO_CLK_ENABLE()   do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)

/* IO操作 */
#define ov5640_SCCB_SCL(x)                  do{ x ?                                                                                             \
                                                    HAL_GPIO_WritePin(ov5640_SCCB_SCL_GPIO_PORT, ov5640_SCCB_SCL_GPIO_PIN, GPIO_PIN_SET) :  \
                                                    HAL_GPIO_WritePin(ov5640_SCCB_SCL_GPIO_PORT, ov5640_SCCB_SCL_GPIO_PIN, GPIO_PIN_RESET); \
                                                }while(0)
#define ov5640_SCCB_SDA(x)                  do{ x ?                                                                                             \
                                                    HAL_GPIO_WritePin(ov5640_SCCB_SDA_GPIO_PORT, ov5640_SCCB_SDA_GPIO_PIN, GPIO_PIN_SET) :  \
                                                    HAL_GPIO_WritePin(ov5640_SCCB_SDA_GPIO_PORT, ov5640_SCCB_SDA_GPIO_PIN, GPIO_PIN_RESET); \
                                                }while(0)
#define ov5640_SCCB_READ_SDA()              HAL_GPIO_ReadPin(ov5640_SCCB_SDA_GPIO_PORT, ov5640_SCCB_SDA_GPIO_PIN)

/* 操作函数 */
void ov5640_sccb_init(void);                                                        /* OV5640 SCCB接口初始化 */
void ov5640_sccb_3_phase_write(uint8_t id_addr, uint16_t sub_addr, uint8_t dat);    /* SCCB接口3阶段写传输 */
void ov5640_sccb_2_phase_write(uint8_t id_addr, uint16_t sub_addr);                 /* SCCB接口2阶段写传输 */
void ov5640_sccb_2_phase_read(uint8_t id_addr, uint8_t *dat);                       /* SCCB接口2阶段读传输 */

#endif
