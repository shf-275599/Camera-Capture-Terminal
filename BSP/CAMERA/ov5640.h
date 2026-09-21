/**
 ****************************************************************************************************
 * @file        ov5640.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2022-06-21
 * @brief       OV5640模块驱动代码
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

#ifndef __OV5640_H
#define __OV5640_H

#include "bsp_sys.h"

/* 定义OV5640模块的闪光灯是否由OV5640控制 */
#define ov5640_LED_CTL_BY_OV5640            1

/* 引脚定义 */
#define ov5640_RST_GPIO_PORT                OV5640_RST_GPIO_Port
#define ov5640_RST_GPIO_PIN                 OV5640_RST_Pin
#define ov5640_PWDN_GPIO_PORT               OV5640_PWDN_GPIO_Port
#define ov5640_PWDN_GPIO_PIN                OV5640_PWDN_Pin

/* IO操作 */
#define ov5640_RST(x)                       do{ x ?                                                                                         \
                                                    HAL_GPIO_WritePin(ov5640_RST_GPIO_PORT, ov5640_RST_GPIO_PIN, GPIO_PIN_SET) :        \
                                                    HAL_GPIO_WritePin(ov5640_RST_GPIO_PORT, ov5640_RST_GPIO_PIN, GPIO_PIN_RESET);       \
                                                }while(0)
#define ov5640_PWDN(x)                      do{ x ?                                                                                         \
                                                    HAL_GPIO_WritePin(ov5640_PWDN_GPIO_PORT, ov5640_PWDN_GPIO_PIN, GPIO_PIN_SET) :                                                        \
                                                    HAL_GPIO_WritePin(ov5640_PWDN_GPIO_PORT, ov5640_PWDN_GPIO_PIN, GPIO_PIN_RESET);                                                         \
                                                }while(0)

/* OV5640 SCCB通讯地址 */
#define ov5640_SCCB_ADDR                    0x3C

/* OV5640模块ISP输入窗口最大尺寸 */
#define ov5640_ISP_INPUT_WIDTH_MAX          0x0A3F
#define ov5640_ISP_INPUT_HEIGHT_MAX         0x06A9

/* OV5640模块灯光模式枚举 */
typedef enum
{
    ov5640_LIGHT_MODE_ADVANCED_AWB = 0x00,  /* Advanced AWB */
    ov5640_LIGHT_MODE_SIMPLE_AWB,           /* Simple AWB */
    ov5640_LIGHT_MODE_MANUAL_DAY,           /* Manual day */
    ov5640_LIGHT_MODE_MANUAL_A,             /* Manual A */
    ov5640_LIGHT_MODE_MANUAL_CWF,           /* Manual cwf */
    ov5640_LIGHT_MODE_MANUAL_CLOUDY         /* Manual cloudy */
} ov5640_light_mode_t;

/* OV5640模块色彩饱和度枚举 */
typedef enum
{
    ov5640_COLOR_SATURATION_0 = 0x00,       /* +4 */
    ov5640_COLOR_SATURATION_1,              /* +3 */
    ov5640_COLOR_SATURATION_2,              /* +2 */
    ov5640_COLOR_SATURATION_3,              /* +1 */
    ov5640_COLOR_SATURATION_4,              /* 0 */
    ov5640_COLOR_SATURATION_5,              /* -1 */
    ov5640_COLOR_SATURATION_6,              /* -2 */
    ov5640_COLOR_SATURATION_7,              /* -3 */
    ov5640_COLOR_SATURATION_8,              /* -4 */
} ov5640_color_saturation_t;

/* OV5640模块亮度枚举 */
typedef enum
{
    ov5640_BRIGHTNESS_0 = 0x00,             /* +4 */
    ov5640_BRIGHTNESS_1,                    /* +3 */
    ov5640_BRIGHTNESS_2,                    /* +2 */
    ov5640_BRIGHTNESS_3,                    /* +1 */
    ov5640_BRIGHTNESS_4,                    /* 0 */
    ov5640_BRIGHTNESS_5,                    /* -1 */
    ov5640_BRIGHTNESS_6,                    /* -2 */
    ov5640_BRIGHTNESS_7,                    /* -3 */
    ov5640_BRIGHTNESS_8,                    /* -4 */
} ov5640_brightness_t;

/* OV5640模块对比度枚举 */
typedef enum
{
    ov5640_CONTRAST_0 = 0x00,               /* +4 */
    ov5640_CONTRAST_1,                      /* +3 */
    ov5640_CONTRAST_2,                      /* +2 */
    ov5640_CONTRAST_3,                      /* +1 */
    ov5640_CONTRAST_4,                      /* 0 */
    ov5640_CONTRAST_5,                      /* -1 */
    ov5640_CONTRAST_6,                      /* -2 */
    ov5640_CONTRAST_7,                      /* -3 */
    ov5640_CONTRAST_8,                      /* -4 */
} ov5640_contrast_t;

/* OV5640模块色相枚举 */
typedef enum
{
    ov5640_HUE_0 = 0x00,                    /* -180 degree */
    ov5640_HUE_1,                           /* -150 degree */
    ov5640_HUE_2,                           /* -120 degree */
    ov5640_HUE_3,                           /* -90 degree */
    ov5640_HUE_4,                           /* -60 degree */
    ov5640_HUE_5,                           /* -30 degree */
    ov5640_HUE_6,                           /* 0 degree */
    ov5640_HUE_7,                           /* +30 degree */
    ov5640_HUE_8,                           /* +60 degree */
    ov5640_HUE_9,                           /* +90 degree */
    ov5640_HUE_10,                          /* +120 degree */
    ov5640_HUE_11,                          /* +150 degree */
} ov5640_hue_t;

/* OV5640模块特殊效果枚举 */
typedef enum
{
    ov5640_SPECIAL_EFFECT_NORMAL = 0x00,    /* Normal */
    ov5640_SPECIAL_EFFECT_BW,               /* B&W */
    ov5640_SPECIAL_EFFECT_BLUISH,           /* Bluish */
    ov5640_SPECIAL_EFFECT_SEPIA,            /* Sepia */
    ov5640_SPECIAL_EFFECT_REDDISH,          /* Reddish */
    ov5640_SPECIAL_EFFECT_GREENISH,         /* Greenish */
    ov5640_SPECIAL_EFFECT_NEGATIVE,         /* Negative */
} ov5640_special_effect_t;

/* OV5640模块曝光度枚举 */
typedef enum
{
    ov5640_EXPOSURE_LEVEL_0 = 0x00,         /* -1.7EV */
    ov5640_EXPOSURE_LEVEL_1,                /* -1.3EV */
    ov5640_EXPOSURE_LEVEL_2,                /* -1.0EV */
    ov5640_EXPOSURE_LEVEL_3,                /* -0.7EV */
    ov5640_EXPOSURE_LEVEL_4,                /* -0.3EV */
    ov5640_EXPOSURE_LEVEL_5,                /* default */
    ov5640_EXPOSURE_LEVEL_6,                /* 0.3EV */
    ov5640_EXPOSURE_LEVEL_7,                /* 0.7EV */
    ov5640_EXPOSURE_LEVEL_8,                /* 1.0EV */
    ov5640_EXPOSURE_LEVEL_9,                /* 1.3EV */
    ov5640_EXPOSURE_LEVEL_10,               /* 1.7EV */
} ov5640_exposure_level_t;

/* OV5640模块锐度枚举 */
typedef enum
{
    ov5640_SHARPNESS_OFF = 0x00,            /* Sharpness OFF */
    ov5640_SHARPNESS_1,                     /* Sharpness 1 */
    ov5640_SHARPNESS_2,                     /* Sharpness 2 */
    ov5640_SHARPNESS_3,                     /* Sharpness 3 */
    ov5640_SHARPNESS_4,                     /* Sharpness 4 */
    ov5640_SHARPNESS_5,                     /* Sharpness 5 */
    ov5640_SHARPNESS_6,                     /* Sharpness 6 */
    ov5640_SHARPNESS_7,                     /* Sharpness 7 */
    ov5640_SHARPNESS_8,                     /* Sharpness 8 */
    ov5640_SHARPNESS_AUTO,                  /* Sharpness Auto */
} ov5640_sharpness_t;

/* OV5640模块镜像/翻转枚举 */
typedef enum
{
    ov5640_MIRROR_FLIP_0 = 0x00,            /* MIRROR */
    ov5640_MIRROR_FLIP_1,                   /* FLIP */
    ov5640_MIRROR_FLIP_2,                   /* MIRROR & FLIP */
    ov5640_MIRROR_FLIP_3,                   /* Normal */
} ov5640_mirror_flip_t;

/* OV5640模块测试图案枚举 */
typedef enum
{
    ov5640_TEST_PATTERN_OFF = 0x00,         /* OFF */
    ov5640_TEST_PATTERN_COLOR_BAR,          /* Color bar */
    ov5640_TEST_PATTERN_COLOR_SQUARE,       /* Color square */
} ov5640_test_pattern_t;

/* OV5640输出图像格式枚举 */
typedef enum
{
    ov5640_OUTPUT_FORMAT_RGB565 = 0x00,     /* RGB565 */
    ov5640_OUTPUT_FORMAT_JPEG,              /* JPEG */
} ov5640_output_format_t;

/* OV5640获取帧数据方式枚举 */
typedef enum
{
    ov5640_GET_TYPE_DTS_8B_NOINC = 0x00,    /* 图像数据以字节方式写入目的地址，目的地址固定不变 */
    ov5640_GET_TYPE_DTS_8B_INC,             /* 图像数据以字节方式写入目的地址，目的地址自动增加 */
    ov5640_GET_TYPE_DTS_16B_NOINC,          /* 图像数据以半字方式写入目的地址，目的地址固定不变 */
    ov5640_GET_TYPE_DTS_16B_INC,            /* 图像数据以半字方式写入目的地址，目的地址自动增加 */
    ov5640_GET_TYPE_DTS_32B_NOINC,          /* 图像数据以字方式写入目的地址，目的地址固定不变 */
    ov5640_GET_TYPE_DTS_32B_INC,            /* 图像数据以字方式写入目的地址，目的地址自动增加 */
} ov5640_get_type_t;

/* 错误代码 */
#define ov5640_EOK      0   /* 没有错误 */
#define ov5640_ERROR    1   /* 错误 */
#define ov5640_EINVAL   2   /* 非法参数 */
#define ov5640_ENOMEM   3   /* 内存不足 */
#define ov5640_EEMPTY   4   /* 资源为空 */
#define ov5640_ETIMEOUT 5   /* 操作超时 */

/* 操作函数 */
uint8_t ov5640_init(void);                                                                              /* 初始OVC5640模块 */
uint8_t ov5640_auto_focus_init(void);                                                                   /* 初始OVC5640模块自动对焦 */
uint8_t ov5640_auto_focus_once(void);                                                                   /* OV5640模块自动对焦一次 */
uint8_t ov5640_auto_focus_continuance(void);                                                            /* OV5640模块持续自动对焦 */
void ov5640_led_on(void);                                                                               /* 开启OV5640模块闪光灯 */
void ov5640_led_off(void);                                                                              /* 关闭OV5640模块闪光灯 */
uint8_t ov5640_set_light_mode(ov5640_light_mode_t mode);                                            /* 设置OV5640模块灯光模式 */
uint8_t ov5640_set_color_saturation(ov5640_color_saturation_t saturation);                          /* 设置OV5640模块色彩饱和度 */
uint8_t ov5640_set_brightness(ov5640_brightness_t brightness);                                      /* 设置OV5640模块亮度 */
uint8_t ov5640_set_contrast(ov5640_contrast_t contrast);                                            /* 设置OV5640模块对比度 */
uint8_t ov5640_set_hue(ov5640_hue_t hue);                                                           /* 设置OV5640模块色相 */
uint8_t ov5640_set_special_effect(ov5640_special_effect_t effect);                                  /* 设置OV5640模块特殊效果 */
uint8_t ov5640_set_exposure_level(ov5640_exposure_level_t level);                                   /* 设置OV5640模块曝光度 */
uint8_t ov5640_set_sharpness_level(ov5640_sharpness_t sharpness);                                   /* 设置OV5640模块锐度 */
uint8_t ov5640_set_mirror_flip(ov5640_mirror_flip_t mirror_flip);                                   /* 设置OV5640模块镜像/翻转 */
uint8_t ov5640_set_test_pattern(ov5640_test_pattern_t pattern);                                     /* 设置OV5640模块测试图案 */
uint8_t ov5640_set_output_format(ov5640_output_format_t format);                                    /* 设置OV5640模块输出图像格式 */
uint8_t ov5640_set_isp_input_window(uint16_t x, uint16_t y, uint16_t width, uint16_t height);           /* 设置OV5640模块ISP输入窗口尺寸 */
uint8_t ov5640_set_pre_scaling_window(uint16_t x_offset, uint16_t y_offset);                            /* 设置OV5640模块预缩放窗口偏移 */
uint8_t ov5640_set_output_size(uint16_t width, uint16_t height);                                        /* 设置OV5640模块输出图像尺寸 */
uint8_t ov5640_get_frame(uint32_t dts_addr, ov5640_get_type_t type, void (*before_transfer)(void)); /* 获取OV5640模块输出的一帧图像数据 */

#endif
