/**
 ****************************************************************************************************
 * @file        ov5640_sccb.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2022-06-21
 * @brief       OV5640模块SCCB接口驱动代码（硬件I2C1版本）
 * @license     Copyright (c) 2020-2032, 广州星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实战平台:正点原子 战舰H743开发板
 * 官方视频:www.yuanzige.com
 * 官方论坛:www.openedv.com
 * 公司地址:www.alientek.com
 * 购买地址:openedv.taobao.com
 *
 ****************************************************************************************************
 */

#include "ov5640_sccb.h"
#include "i2c.h"

/* I2C超时时间，单位毫秒(ms) */
#define OV5640_I2C_TIMEOUT  100

/* 2阶段写+2阶段读 需要缓存寄存器地址 */
static uint16_t s_sub_addr;

/**
 * @brief       OV5640 SCCB接口初始化（使用硬件I2C1）
 * @param       无
 * @retval      无
 */
void ov5640_sccb_init(void)
{
    /* 硬件I2C1已在CubeMX中初始化，此处无需额外操作 */
}

/**
 * @brief       SCCB接口3阶段写数据（ID地址 + 16位寄存器地址 + 数据）
 * @param       id_addr : ID Address (7位地址，左移由HAL处理)
 *              sub_addr: Sub-address (16位寄存器地址)
 *              dat     : Write Data
 * @retval      无
 */
void ov5640_sccb_3_phase_write(uint8_t id_addr, uint16_t sub_addr, uint8_t dat)
{
    HAL_I2C_Mem_Write(&hi2c1, id_addr << 1, sub_addr, I2C_MEMADD_SIZE_16BIT, &dat, 1, OV5640_I2C_TIMEOUT);
}

/**
 * @brief       SCCB接口2阶段写数据（设置寄存器指针）
 * @param       id_addr : ID Address
 *              sub_addr: Sub-address (16位寄存器地址)
 * @retval      无
 */
void ov5640_sccb_2_phase_write(uint8_t id_addr, uint16_t sub_addr)
{
    s_sub_addr = sub_addr;
}

/**
 * @brief       SCCB接口2阶段读数据（从上次设置的寄存器地址读取）
 * @param       id_addr: ID Address
 *              dat    : 读取到的数据
 * @retval      无
 */
void ov5640_sccb_2_phase_read(uint8_t id_addr, uint8_t *dat)
{
    HAL_I2C_Mem_Read(&hi2c1, id_addr << 1, s_sub_addr, I2C_MEMADD_SIZE_16BIT, dat, 1, OV5640_I2C_TIMEOUT);
}
