/**
 ****************************************************************************************************
 * @file        ov5640_sccb.c
 * @brief       OV5640模块SCCB接口驱动代码（软件模拟，PB6=SCL, PB7=SDA）
 ****************************************************************************************************
 */

#include "ov5640_sccb.h"
#include "bsp_delay.h"

#define ov5640_SCCB_WRITE   0x00
#define ov5640_SCCB_READ    0x01

static void ov5640_sccb_set_sda_output(void)
{
    GPIO_InitTypeDef gpio_init_struct = {0};
    gpio_init_struct.Pin    = OV5640_SDA_Pin;
    gpio_init_struct.Mode   = GPIO_MODE_OUTPUT_PP;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(OV5640_SDA_GPIO_Port, &gpio_init_struct);
}

static void ov5640_sccb_set_sda_input(void)
{
    GPIO_InitTypeDef gpio_init_struct = {0};
    gpio_init_struct.Pin    = OV5640_SDA_Pin;
    gpio_init_struct.Mode   = GPIO_MODE_INPUT;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(OV5640_SDA_GPIO_Port, &gpio_init_struct);
}

static inline void ov5640_sccb_delay(void)
{
    delay_us(5);
}

static void ov5640_sccb_start(void)
{
    OV5640_SCCB_SDA(1);
    OV5640_SCCB_SCL(1);
    ov5640_sccb_delay();
    OV5640_SCCB_SDA(0);
    ov5640_sccb_delay();
    OV5640_SCCB_SCL(0);
}

static void ov5640_sccb_stop(void)
{
    OV5640_SCCB_SDA(0);
    ov5640_sccb_delay();
    OV5640_SCCB_SCL(1);
    ov5640_sccb_delay();
    OV5640_SCCB_SDA(1);
    ov5640_sccb_delay();
}

static void ov5640_sccb_write_byte(uint8_t dat)
{
    int8_t dat_index;
    uint8_t dat_bit;

    for (dat_index = 7; dat_index >= 0; dat_index--)
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

static void ov5640_sccb_read_byte(uint8_t *dat)
{
    int8_t dat_index;
    uint8_t dat_bit;

    ov5640_sccb_set_sda_input();

    *dat = 0;
    for (dat_index = 7; dat_index >= 0; dat_index--)
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

    ov5640_sccb_set_sda_output();
}

void ov5640_sccb_init(void)
{
    ov5640_sccb_stop();
}

void ov5640_sccb_3_phase_write(uint8_t id_addr, uint16_t sub_addr, uint8_t dat)
{
    ov5640_sccb_start();
    ov5640_sccb_write_byte((id_addr << 1) | ov5640_SCCB_WRITE);
    ov5640_sccb_write_byte((uint8_t)(sub_addr >> 8) & 0xFF);
    ov5640_sccb_write_byte((uint8_t)sub_addr & 0xFF);
    ov5640_sccb_write_byte(dat);
    ov5640_sccb_stop();
}

void ov5640_sccb_2_phase_write(uint8_t id_addr, uint16_t sub_addr)
{
    ov5640_sccb_start();
    ov5640_sccb_write_byte((id_addr << 1) | ov5640_SCCB_WRITE);
    ov5640_sccb_write_byte((uint8_t)(sub_addr >> 8) & 0xFF);
    ov5640_sccb_write_byte((uint8_t)sub_addr & 0xFF);
    ov5640_sccb_stop();
}

void ov5640_sccb_2_phase_read(uint8_t id_addr, uint8_t *dat)
{
    ov5640_sccb_start();
    ov5640_sccb_write_byte((id_addr << 1) | ov5640_SCCB_READ);
    ov5640_sccb_read_byte(dat);
    ov5640_sccb_stop();
}
