/**
 ****************************************************************************************************
 * @file        ov5640_sccb.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2022-06-21
 * @brief       OV5640模块SCCB接口驱动代码
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
#include "bsp_delay.h"

/* SCCB接口读写通讯地址bit0 */
#define ov5640_SCCB_WRITE   0x00
#define ov5640_SCCB_READ    0x01

#if (OV5640_SCCB_GPIO_PULLUP != 0)
/**
 * @brief       设置SCCB接口SDA线为输出模式
 * @param       
 * @retval      
 */
static void ov5640_sccb_set_sda_output(void)
{
    GPIO_InitTypeDef gpio_init_struct = {0};
    
    gpio_init_struct.Pin    = OV5640_SCCB_SDA_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_OUTPUT_PP;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(OV5640_SCCB_SDA_GPIO_PORT, &gpio_init_struct);
}

/**
 * @brief       设置SCCB接口SDA线为输入模式
 * @param       
 * @retval      
 */
static void ov5640_sccb_set_sda_input(void)
{
    GPIO_InitTypeDef gpio_init_struct = {0};
    
    gpio_init_struct.Pin    = OV5640_SCCB_SDA_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_INPUT;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(OV5640_SCCB_SDA_GPIO_PORT, &gpio_init_struct);
}
#endif

/**
 * @brief       SCCB接口延时函数
 * @param       
 * @retval      
 */
static inline void ov5640_sccb_delay(void)
{
    delay_us(5);
}

/**
 * @brief       SCCB接口起始信号
 * @param       
 * @retval      
 */
static void ov5640_sccb_start(void)
{
    OV5640_SCCB_SDA(1);
    OV5640_SCCB_SCL(1);
    ov5640_sccb_delay();
    OV5640_SCCB_SDA(0);
    ov5640_sccb_delay();
    OV5640_SCCB_SCL(0);
}

/**
 * @brief       SCCB接口停止信号
 * @param       
 * @retval      
 */
static void ov5640_sccb_stop(void)
{
    OV5640_SCCB_SDA(0);
    ov5640_sccb_delay();
    OV5640_SCCB_SCL(1);
    ov5640_sccb_delay();
    OV5640_SCCB_SDA(1);
    ov5640_sccb_delay();
}

/**
 * @brief       SCCB接口向从机写入一个字节数据
 * @param       dat: 要写入的一个字节数据
 * @retval      
 */
static void ov5640_sccb_write_byte(uint8_t dat)
{
    int8_t dat_index;
    uint8_t dat_bit;
    
    for (dat_index=7; dat_index>=0; dat_index--)
    {
        dat_bit = (dat >> dat_index) & 0x01;
        OV5640_SCCB_SDA(dat_bit);
        ov5640_sccb_delay();
        OV5640_SCCB_SCL(1);
        ov5640_sccb_delay();
        OV5640_SCCB_SCL(0);
    }
    
    OV5640_SCCB_SDA(1);
    ov5640_sccb_delay();
    OV5640_SCCB_SCL(1);
    ov5640_sccb_delay();
    OV5640_SCCB_SCL(0);
}

/**
 * @brief       SCCB接口从从机读取一个字节数据
 * @param       dat: 读取到的一个字节数据
 * @retval      
 */
static void ov5640_sccb_read_byte(uint8_t *dat)
{
    int8_t dat_index;
    uint8_t dat_bit;
    
#if (OV5640_SCCB_GPIO_PULLUP != 0)
    ov5640_sccb_set_sda_input();
#endif
    
    for (dat_index=7; dat_index>=0; dat_index--)
    {
        ov5640_sccb_delay();
        OV5640_SCCB_SCL(1);
        dat_bit = OV5640_SCCB_READ_SDA();
        *dat |= (dat_bit << dat_index);
        ov5640_sccb_delay();
        OV5640_SCCB_SCL(0);
    }
    
    ov5640_sccb_delay();
    OV5640_SCCB_SCL(1);
    ov5640_sccb_delay();
    OV5640_SCCB_SCL(0);
    ov5640_sccb_delay();
    OV5640_SCCB_SDA(0);
    ov5640_sccb_delay();
    
#if (OV5640_SCCB_GPIO_PULLUP != 0)
    ov5640_sccb_set_sda_output();
#endif
}

/**
 * @brief       OV5640 SCCB接口初始化
 * @param       
 * @retval      
 */
void ov5640_sccb_init(void)
{
    GPIO_InitTypeDef gpio_init_struct = {0};
    
    /* 使能SCL和SDA引脚GPIO时钟 */
    OV5640_SCCB_SCL_GPIO_CLK_ENABLE();
    OV5640_SCCB_SDA_GPIO_CLK_ENABLE();
    
    /* 初始化SCL引脚 */
    gpio_init_struct.Pin    = OV5640_SCCB_SCL_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_OUTPUT_PP;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(OV5640_SCCB_SCL_GPIO_PORT, &gpio_init_struct);
    
    /* 初始化SDA引脚 */
    gpio_init_struct.Pin    = OV5640_SCCB_SDA_GPIO_PIN;
#if (OV5640_SCCB_GPIO_PULLUP != 0)
    gpio_init_struct.Mode   = GPIO_MODE_OUTPUT_PP;
#else
    gpio_init_struct.Mode   = GPIO_MODE_OUTPUT_OD;
#endif
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(OV5640_SCCB_SDA_GPIO_PORT, &gpio_init_struct);
    
    ov5640_sccb_stop();
}

/**
 * @brief       SCCB接口3阶段写数据
 * @param       id_addr : ID Address
 *              sub_addr: Sub-address
 *              dat     : Write Data
 * @retval      
 */
void ov5640_sccb_3_phase_write(uint8_t id_addr, uint16_t sub_addr, uint8_t dat)
{
    ov5640_sccb_start();
    ov5640_sccb_write_byte((id_addr << 1) | ov5640_SCCB_WRITE);
    ov5640_sccb_write_byte((uint8_t)(sub_addr >> 8) & 0xFF);
    ov5640_sccb_write_byte((uint8_t)sub_addr & 0xFF);
    ov5640_sccb_write_byte(dat);
    ov5640_sccb_stop();
}

/**
 * @brief       SCCB接口2阶段写数据
 * @param       id_addr : ID Address
 *              sub_addr: Sub-address
 * @retval      
 */
void ov5640_sccb_2_phase_write(uint8_t id_addr, uint16_t sub_addr)
{
    ov5640_sccb_start();
    ov5640_sccb_write_byte((id_addr << 1) | ov5640_SCCB_WRITE);
    ov5640_sccb_write_byte((uint8_t)(sub_addr >> 8) & 0xFF);
    ov5640_sccb_write_byte((uint8_t)sub_addr & 0xFF);
    ov5640_sccb_stop();
}

/**
 * @brief       SCCB接口2阶段读数据
 * @param       id_addr: ID Address
 *              dat: 读取到的数据
 * @retval      
 */
void ov5640_sccb_2_phase_read(uint8_t id_addr, uint8_t *dat)
{
    ov5640_sccb_start();
    ov5640_sccb_write_byte((id_addr << 1) | ov5640_SCCB_READ);
    ov5640_sccb_read_byte(dat);
    ov5640_sccb_stop();
}
