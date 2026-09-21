/**
 ****************************************************************************************************
 * @file        ov5640_sccb.c
 * @author      ����ԭ���Ŷ�(ALIENTEK)
 * @version     V1.0
 * @date        2022-06-21
 * @brief       OV5640ģ��SCCB�ӿ���������
 * @license     Copyright (c) 2020-2032, �������������ӿƼ����޹�˾
 ****************************************************************************************************
 * @attention
 *
 * ʵ��ƽ̨:����ԭ�� ������ H743������
 * ������Ƶ:www.yuanzige.com
 * ������̳:www.openedv.com
 * ��˾��ַ:www.alientek.com
 * �����ַ:openedv.taobao.com
 *
 ****************************************************************************************************
 */

#include "ov5640_sccb.h"
#include "bsp_delay.h"

/* SCCB�ӿڶ�дͨѶ��ַbit0 */
#define ov5640_SCCB_WRITE   0x00
#define ov5640_SCCB_READ    0x01

#if (OV5640_SCCB_GPIO_PULLUP != 0)
/**
 * @brief       ����SCCB�ӿ�SDA����Ϊ���ģʽ
 * @param       ��
 * @retval      ��
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
 * @brief       ����SCCB�ӿ�SDA����Ϊ����ģʽ
 * @param       ��
 * @retval      ��
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
 * @brief       SCCB�ӿ���ʱ����
 * @param       ��
 * @retval      ��
 */
static inline void ov5640_sccb_delay(void)
{
    delay_us(5);
}

/**
 * @brief       SCCB�ӿ���ʼ�ź�
 * @param       ��
 * @retval      ��
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
 * @brief       SCCB�ӿ�ֹͣ�ź�
 * @param       ��
 * @retval      ��
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
 * @brief       SCCB�ӿ�������ӻ�д��һ���ֽ�����
 * @param       dat: ��д���һ���ֽ�����
 * @retval      ��
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
 * @brief       SCCB�ӿ�������ӻ���ȡһ���ֽ�����
 * @param       dat: ��ȡ����һ���ֽ�����
 * @retval      ��
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
 * @brief       OV5640 SCCB�ӿڳ�ʼ��
 * @param       ��
 * @retval      ��
 */
void ov5640_sccb_init(void)
{
    GPIO_InitTypeDef gpio_init_struct = {0};
    
    /* ʹ��SCL��SDA����GPIOʱ�� */
    OV5640_SCCB_SCL_GPIO_CLK_ENABLE();
    OV5640_SCCB_SDA_GPIO_CLK_ENABLE();
    
    /* ��ʼ��SCL���� */
    gpio_init_struct.Pin    = OV5640_SCCB_SCL_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_OUTPUT_PP;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(OV5640_SCCB_SCL_GPIO_PORT, &gpio_init_struct);
    
    /* ��ʼ��SDA���� */
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
 * @brief       SCCB�ӿ�3�׶�д����
 * @param       id_addr : ID Address
 *              sub_addr: Sub-address
 *              dat     : Write Data
 * @retval      ��
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
 * @brief       SCCB�ӿ�2�׶�д����
 * @param       id_addr : ID Address
 *              sub_addr: Sub-address
 * @retval      ��
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
 * @brief       SCCB�ӿ�2�׶ζ�����
 * @param       id_addr: ID Address
 *              dat: ��ȡ��������
 * @retval      ��
 */
void ov5640_sccb_2_phase_read(uint8_t id_addr, uint8_t *dat)
{
    ov5640_sccb_start();
    ov5640_sccb_write_byte((id_addr << 1) | ov5640_SCCB_READ);
    ov5640_sccb_read_byte(dat);
    ov5640_sccb_stop();
}
