/**
 ****************************************************************************************************
 * @file        ov5640.h
 * @author      ����ԭ���Ŷ�(ALIENTEK)
 * @version     V1.0
 * @date        2022-06-21
 * @brief       OV5640ģ����������
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

#ifndef __OV5640_H
#define __OV5640_H

#include "bsp_sys.h"

/* ���Ŷ��� */
#define OV5640_RST_GPIO_PORT                OV5640_RST_GPIO_Port
#define OV5640_RST_GPIO_PIN                 OV5640_RST_Pin
#define OV5640_PWDN_GPIO_PORT               OV5640_PWDN_GPIO_Port
#define OV5640_PWDN_GPIO_PIN                OV5640_PWDN_Pin

/* IO���� */
#define OV5640_RST(x)                       do{ x ?                                                                                         \
                                                    HAL_GPIO_WritePin(OV5640_RST_GPIO_PORT, OV5640_RST_GPIO_PIN, GPIO_PIN_SET) :        \
                                                    HAL_GPIO_WritePin(OV5640_RST_GPIO_PORT, OV5640_RST_GPIO_PIN, GPIO_PIN_RESET);       \
                                                }while(0)
#define OV5640_PWDN(x)                      do{ x ?                                                                                         \
                                                    HAL_GPIO_WritePin(OV5640_PWDN_GPIO_PORT, OV5640_PWDN_GPIO_PIN, GPIO_PIN_SET) :                                                        \
                                                    HAL_GPIO_WritePin(OV5640_PWDN_GPIO_PORT, OV5640_PWDN_GPIO_PIN, GPIO_PIN_RESET);                                                         \
                                                }while(0)

/* OV5640 SCCBͨѶ��ַ */
#define OV5640_SCCB_ADDR                    0x3C

/* OV5640ģ��ISP���봰�����ߴ� */
#define OV5640_ISP_INPUT_WIDTH_MAX          0x0A3F
#define OV5640_ISP_INPUT_HEIGHT_MAX         0x06A9

/* OV5640ģ��ƹ�ģʽö�� */
typedef enum
{
    OV5640_LIGHT_MODE_ADVANCED_AWB = 0x00,  /* Advanced AWB */
    OV5640_LIGHT_MODE_SIMPLE_AWB,           /* Simple AWB */
    OV5640_LIGHT_MODE_MANUAL_DAY,           /* Manual day */
    OV5640_LIGHT_MODE_MANUAL_A,             /* Manual A */
    OV5640_LIGHT_MODE_MANUAL_CWF,           /* Manual cwf */
    OV5640_LIGHT_MODE_MANUAL_CLOUDY         /* Manual cloudy */
} ov5640_light_mode_t;

/* OV5640ģ��ɫ�ʱ��Ͷ�ö�� */
typedef enum
{
    OV5640_COLOR_SATURATION_0 = 0x00,       /* +4 */
    OV5640_COLOR_SATURATION_1,              /* +3 */
    OV5640_COLOR_SATURATION_2,              /* +2 */
    OV5640_COLOR_SATURATION_3,              /* +1 */
    OV5640_COLOR_SATURATION_4,              /* 0 */
    OV5640_COLOR_SATURATION_5,              /* -1 */
    OV5640_COLOR_SATURATION_6,              /* -2 */
    OV5640_COLOR_SATURATION_7,              /* -3 */
    OV5640_COLOR_SATURATION_8,              /* -4 */
} ov5640_color_saturation_t;

/* OV5640ģ������ö�� */
typedef enum
{
    OV5640_BRIGHTNESS_0 = 0x00,             /* +4 */
    OV5640_BRIGHTNESS_1,                    /* +3 */
    OV5640_BRIGHTNESS_2,                    /* +2 */
    OV5640_BRIGHTNESS_3,                    /* +1 */
    OV5640_BRIGHTNESS_4,                    /* 0 */
    OV5640_BRIGHTNESS_5,                    /* -1 */
    OV5640_BRIGHTNESS_6,                    /* -2 */
    OV5640_BRIGHTNESS_7,                    /* -3 */
    OV5640_BRIGHTNESS_8,                    /* -4 */
} ov5640_brightness_t;

/* OV5640ģ��Աȶ�ö�� */
typedef enum
{
    OV5640_CONTRAST_0 = 0x00,               /* +4 */
    OV5640_CONTRAST_1,                      /* +3 */
    OV5640_CONTRAST_2,                      /* +2 */
    OV5640_CONTRAST_3,                      /* +1 */
    OV5640_CONTRAST_4,                      /* 0 */
    OV5640_CONTRAST_5,                      /* -1 */
    OV5640_CONTRAST_6,                      /* -2 */
    OV5640_CONTRAST_7,                      /* -3 */
    OV5640_CONTRAST_8,                      /* -4 */
} ov5640_contrast_t;

/* OV5640ģ��ɫ��ö�� */
typedef enum
{
    OV5640_HUE_0 = 0x00,                    /* -180 degree */
    OV5640_HUE_1,                           /* -150 degree */
    OV5640_HUE_2,                           /* -120 degree */
    OV5640_HUE_3,                           /* -90 degree */
    OV5640_HUE_4,                           /* -60 degree */
    OV5640_HUE_5,                           /* -30 degree */
    OV5640_HUE_6,                           /* 0 degree */
    OV5640_HUE_7,                           /* +30 degree */
    OV5640_HUE_8,                           /* +60 degree */
    OV5640_HUE_9,                           /* +90 degree */
    OV5640_HUE_10,                          /* +120 degree */
    OV5640_HUE_11,                          /* +150 degree */
} ov5640_hue_t;

/* OV5640ģ������Ч��ö�� */
typedef enum
{
    OV5640_SPECIAL_EFFECT_NORMAL = 0x00,    /* Normal */
    OV5640_SPECIAL_EFFECT_BW,               /* B&W */
    OV5640_SPECIAL_EFFECT_BLUISH,           /* Bluish */
    OV5640_SPECIAL_EFFECT_SEPIA,            /* Sepia */
    OV5640_SPECIAL_EFFECT_REDDISH,          /* Reddish */
    OV5640_SPECIAL_EFFECT_GREENISH,         /* Greenish */
    OV5640_SPECIAL_EFFECT_NEGATIVE,         /* Negative */
} ov5640_special_effect_t;

/* OV5640ģ���ع��ö�� */
typedef enum
{
    OV5640_EXPOSURE_LEVEL_0 = 0x00,         /* -1.7EV */
    OV5640_EXPOSURE_LEVEL_1,                /* -1.3EV */
    OV5640_EXPOSURE_LEVEL_2,                /* -1.0EV */
    OV5640_EXPOSURE_LEVEL_3,                /* -0.7EV */
    OV5640_EXPOSURE_LEVEL_4,                /* -0.3EV */
    OV5640_EXPOSURE_LEVEL_5,                /* default */
    OV5640_EXPOSURE_LEVEL_6,                /* 0.3EV */
    OV5640_EXPOSURE_LEVEL_7,                /* 0.7EV */
    OV5640_EXPOSURE_LEVEL_8,                /* 1.0EV */
    OV5640_EXPOSURE_LEVEL_9,                /* 1.3EV */
    OV5640_EXPOSURE_LEVEL_10,               /* 1.7EV */
} ov5640_exposure_level_t;

/* OV5640ģ�����ö�� */
typedef enum
{
    OV5640_SHARPNESS_OFF = 0x00,            /* Sharpness OFF */
    OV5640_SHARPNESS_1,                     /* Sharpness 1 */
    OV5640_SHARPNESS_2,                     /* Sharpness 2 */
    OV5640_SHARPNESS_3,                     /* Sharpness 3 */
    OV5640_SHARPNESS_4,                     /* Sharpness 4 */
    OV5640_SHARPNESS_5,                     /* Sharpness 5 */
    OV5640_SHARPNESS_6,                     /* Sharpness 6 */
    OV5640_SHARPNESS_7,                     /* Sharpness 7 */
    OV5640_SHARPNESS_8,                     /* Sharpness 8 */
    OV5640_SHARPNESS_AUTO,                  /* Sharpness Auto */
} ov5640_sharpness_t;

/* OV5640ģ�龵��/��תö�� */
typedef enum
{
    OV5640_MIRROR_FLIP_0 = 0x00,            /* MIRROR */
    OV5640_MIRROR_FLIP_1,                   /* FLIP */
    OV5640_MIRROR_FLIP_2,                   /* MIRROR & FLIP */
    OV5640_MIRROR_FLIP_3,                   /* Normal */
} ov5640_mirror_flip_t;

/* OV5640ģ�����ͼ��ö�� */
typedef enum
{
    OV5640_TEST_PATTERN_OFF = 0x00,         /* OFF */
    OV5640_TEST_PATTERN_COLOR_BAR,          /* Color bar */
    OV5640_TEST_PATTERN_COLOR_SQUARE,       /* Color square */
} ov5640_test_pattern_t;

/* OV5640���ͼ���ʽö�� */
typedef enum
{
    OV5640_OUTPUT_FORMAT_RGB565 = 0x00,     /* RGB565 */
    OV5640_OUTPUT_FORMAT_JPEG,              /* JPEG */
} ov5640_output_format_t;

/* OV5640��ȡ֡���ݷ�ʽö�� */
typedef enum
{
    OV5640_GET_TYPE_DTS_8B_NOINC = 0x00,    /* ͼ���������ֽڷ�ʽд��Ŀ�ĵ�ַ��Ŀ�ĵ�ַ�̶����� */
    OV5640_GET_TYPE_DTS_8B_INC,             /* ͼ���������ֽڷ�ʽд��Ŀ�ĵ�ַ��Ŀ�ĵ�ַ�Զ����� */
    OV5640_GET_TYPE_DTS_16B_NOINC,          /* ͼ�������԰��ַ�ʽд��Ŀ�ĵ�ַ��Ŀ�ĵ�ַ�̶����� */
    OV5640_GET_TYPE_DTS_16B_INC,            /* ͼ�������԰��ַ�ʽд��Ŀ�ĵ�ַ��Ŀ�ĵ�ַ�Զ����� */
    OV5640_GET_TYPE_DTS_32B_NOINC,          /* ͼ���������ַ�ʽд��Ŀ�ĵ�ַ��Ŀ�ĵ�ַ�̶����� */
    OV5640_GET_TYPE_DTS_32B_INC,            /* ͼ���������ַ�ʽд��Ŀ�ĵ�ַ��Ŀ�ĵ�ַ�Զ����� */
} ov5640_get_type_t;

/* ������� */
#define OV5640_EOK      0   /* û�д��� */
#define OV5640_ERROR    1   /* ���� */
#define OV5640_EINVAL   2   /* �Ƿ����� */
#define OV5640_ENOMEM   3   /* �ڴ治�� */
#define OV5640_EEMPTY   4   /* ��ԴΪ�� */
#define OV5640_ETIMEOUT 5   /* ������ʱ */

/* �������� */
uint8_t ov5640_init(void);                                                                              /* ��ʼOVC5640ģ�� */
uint8_t ov5640_auto_focus_init(void);                                                                   /* ��ʼOVC5640ģ���Զ��Խ� */
uint8_t ov5640_auto_focus_once(void);                                                                   /* OV5640ģ���Զ��Խ�һ�� */
uint8_t ov5640_auto_focus_continuance(void);                                                            /* OV5640ģ������Զ��Խ� */
void ov5640_led_on(void);                                                                               /* ����OV5640ģ������� */
void ov5640_led_off(void);                                                                              /* �ر�OV5640ģ������� */
uint8_t ov5640_set_light_mode(ov5640_light_mode_t mode);                                            /* ����OV5640ģ��ƹ�ģʽ */
uint8_t ov5640_set_color_saturation(ov5640_color_saturation_t saturation);                          /* ����OV5640ģ��ɫ�ʱ��Ͷ� */
uint8_t ov5640_set_brightness(ov5640_brightness_t brightness);                                      /* ����OV5640ģ������ */
uint8_t ov5640_set_contrast(ov5640_contrast_t contrast);                                            /* ����OV5640ģ��Աȶ� */
uint8_t ov5640_set_hue(ov5640_hue_t hue);                                                           /* ����OV5640ģ��ɫ�� */
uint8_t ov5640_set_special_effect(ov5640_special_effect_t effect);                                  /* ����OV5640ģ������Ч�� */
uint8_t ov5640_set_exposure_level(ov5640_exposure_level_t level);                                   /* ����OV5640ģ���ع�� */
uint8_t ov5640_set_sharpness_level(ov5640_sharpness_t sharpness);                                   /* ����OV5640ģ����� */
uint8_t ov5640_set_mirror_flip(ov5640_mirror_flip_t mirror_flip);                                   /* ����OV5640ģ�龵��/��ת */
uint8_t ov5640_set_test_pattern(ov5640_test_pattern_t pattern);                                     /* ����OV5640ģ�����ͼ�� */
uint8_t ov5640_set_output_format(ov5640_output_format_t format);                                    /* ����OV5640ģ�����ͼ���ʽ */
uint8_t ov5640_set_isp_input_window(uint16_t x, uint16_t y, uint16_t width, uint16_t height);           /* ����OV5640ģ��ISP���봰�ڳߴ� */
uint8_t ov5640_set_pre_scaling_window(uint16_t x_offset, uint16_t y_offset);                            /* ����OV5640ģ��Ԥ���Ŵ���ƫ�� */
uint8_t ov5640_set_output_size(uint16_t width, uint16_t height);                                        /* ����OV5640ģ�����ͼ��ߴ� */
uint8_t ov5640_get_frame(uint32_t dts_addr, ov5640_get_type_t type, void (*before_transfer)(void)); /* ��ȡOV5640ģ�������һ֡ͼ������ */

#endif
