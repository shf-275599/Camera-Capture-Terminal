/**
 ****************************************************************************************************
 * @file        ov5640.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2022-06-21
 * @brief       OV5640模块驱动代码
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

#ifndef __OV5640_H
#define __OV5640_H

#include "bsp_sys.h"

/* 引脚定义 */
#define OV5640_RST_GPIO_PORT                OV5640_RST_GPIO_Port
#define OV5640_RST_GPIO_PIN                 OV5640_RST_Pin
#define OV5640_PWDN_GPIO_PORT               OV5640_PWDN_GPIO_Port
#define OV5640_PWDN_GPIO_PIN                OV5640_PWDN_Pin

/* IO操作 */
#define OV5640_RST(x)                       do{ x ?                                                                                         \
                                                    HAL_GPIO_WritePin(OV5640_RST_GPIO_PORT, OV5640_RST_GPIO_PIN, GPIO_PIN_SET) :        \
                                                    HAL_GPIO_WritePin(OV5640_RST_GPIO_PORT, OV5640_RST_GPIO_PIN, GPIO_PIN_RESET);       \
                                                }while(0)
#define OV5640_PWDN(x)                      do{ x ?                                                                                         \
                                                    HAL_GPIO_WritePin(OV5640_PWDN_GPIO_PORT, OV5640_PWDN_GPIO_PIN, GPIO_PIN_SET) :                                                        \
                                                    HAL_GPIO_WritePin(OV5640_PWDN_GPIO_PORT, OV5640_PWDN_GPIO_PIN, GPIO_PIN_RESET);                                                         \
                                                }while(0)

/* OV5640 SCCB通讯地址 */
#define OV5640_SCCB_ADDR                    0x3C

/* OV5640模块ISP输入窗口最大坐标值 */
#define OV5640_ISP_INPUT_WIDTH_MAX          0x0A3F
#define OV5640_ISP_INPUT_HEIGHT_MAX         0x06A9

/* OV5640模块光线模式枚举 */
typedef enum
{
    OV5640_LIGHT_MODE_ADVANCED_AWB = 0x00,  /* Advanced AWB */
    OV5640_LIGHT_MODE_SIMPLE_AWB,           /* Simple AWB */
    OV5640_LIGHT_MODE_MANUAL_DAY,           /* Manual day */
    OV5640_LIGHT_MODE_MANUAL_A,             /* Manual A */
    OV5640_LIGHT_MODE_MANUAL_CWF,           /* Manual cwf */
    OV5640_LIGHT_MODE_MANUAL_CLOUDY         /* Manual cloudy */
} ov5640_light_mode_t;

/* OV5640模块色彩饱和度枚举 */
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

/* OV5640模块亮度枚举 */
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

/* OV5640模块对比度枚举 */
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

/* OV5640模块色相枚举 */
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

/* OV5640模块特殊效果枚举 */
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

/* OV5640模块曝光枚举 */
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

/* OV5640模块锐度枚举 */
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

/* OV5640模块镜像/翻转枚举 */
typedef enum
{
    OV5640_MIRROR_FLIP_0 = 0x00,            /* MIRROR */
    OV5640_MIRROR_FLIP_1,                   /* FLIP */
    OV5640_MIRROR_FLIP_2,                   /* MIRROR & FLIP */
    OV5640_MIRROR_FLIP_3,                   /* Normal */
} ov5640_mirror_flip_t;

/* OV5640模块测试图枚举 */
typedef enum
{
    OV5640_TEST_PATTERN_OFF = 0x00,         /* OFF */
    OV5640_TEST_PATTERN_COLOR_BAR,          /* Color bar */
    OV5640_TEST_PATTERN_COLOR_SQUARE,       /* Color square */
} ov5640_test_pattern_t;

/* OV5640输出图像格式枚举 */
typedef enum
{
    OV5640_OUTPUT_FORMAT_RGB565 = 0x00,     /* RGB565 */
    OV5640_OUTPUT_FORMAT_JPEG,              /* JPEG */
} ov5640_output_format_t;

/* OV5640获取帧数据方式枚举 */
typedef enum
{
    OV5640_GET_TYPE_DTS_8B_NOINC = 0x00,    /* 图像数据按字节方式写入目的地址，目的地址固定不变 */
    OV5640_GET_TYPE_DTS_8B_INC,             /* 图像数据按字节方式写入目的地址，目的地址自动递增 */
    OV5640_GET_TYPE_DTS_16B_NOINC,          /* 图像数据可以按半字方式写入目的地址，目的地址固定不变 */
    OV5640_GET_TYPE_DTS_16B_INC,            /* 图像数据可以按半字方式写入目的地址，目的地址自动递增 */
    OV5640_GET_TYPE_DTS_32B_NOINC,          /* 图像数据按字方式写入目的地址，目的地址固定不变 */
    OV5640_GET_TYPE_DTS_32B_INC,            /* 图像数据按字方式写入目的地址，目的地址自动递增 */
} ov5640_get_type_t;

/* 错误码定义 */
#define OV5640_EOK      0   /* 没有错误 */
#define OV5640_ERROR    1   /* 错误 */
#define OV5640_EINVAL   2   /* 非法参数 */
#define OV5640_ENOMEM   3   /* 内存不足 */
#define OV5640_EEMPTY   4   /* 资源为空 */
#define OV5640_ETIMEOUT 5   /* 超时错误 */

/* 函数声明 */
uint8_t ov5640_init(void);                                                                              /* 初始化OV5640模块 */
uint8_t ov5640_auto_focus_init(void);                                                                   /* 初始化OV5640模块自动对焦 */
uint8_t ov5640_auto_focus_once(void);                                                                   /* OV5640模块自动对焦一次 */
uint8_t ov5640_auto_focus_continuance(void);                                                            /* OV5640模块连续自动对焦 */
void ov5640_led_on(void);                                                                               /* 打开OV5640模块补光灯 */
void ov5640_led_off(void);                                                                              /* 关闭OV5640模块补光灯 */
uint8_t ov5640_set_light_mode(ov5640_light_mode_t mode);                                            /* 设置OV5640模块光线模式 */
uint8_t ov5640_set_color_saturation(ov5640_color_saturation_t saturation);                          /* 设置OV5640模块色彩饱和度 */
uint8_t ov5640_set_brightness(ov5640_brightness_t brightness);                                      /* 设置OV5640模块亮度 */
uint8_t ov5640_set_contrast(ov5640_contrast_t contrast);                                            /* 设置OV5640模块对比度 */
uint8_t ov5640_set_hue(ov5640_hue_t hue);                                                           /* 设置OV5640模块色相 */
uint8_t ov5640_set_special_effect(ov5640_special_effect_t effect);                                  /* 设置OV5640模块特殊效果 */
uint8_t ov5640_set_exposure_level(ov5640_exposure_level_t level);                                   /* 设置OV5640模块曝光 */
uint8_t ov5640_set_sharpness_level(ov5640_sharpness_t sharpness);                                   /* 设置OV5640模块锐度 */
uint8_t ov5640_set_mirror_flip(ov5640_mirror_flip_t mirror_flip);                                   /* 设置OV5640模块镜像/翻转 */
uint8_t ov5640_set_test_pattern(ov5640_test_pattern_t pattern);                                     /* 设置OV5640模块测试图 */
uint8_t ov5640_set_output_format(ov5640_output_format_t format);                                    /* 设置OV5640模块输出图像格式 */
uint8_t ov5640_set_isp_input_window(uint16_t x, uint16_t y, uint16_t width, uint16_t height);           /* 设置OV5640模块ISP输入窗口尺寸 */
uint8_t ov5640_set_pre_scaling_window(uint16_t x_offset, uint16_t y_offset);                            /* 设置OV5640模块预览放大器偏移 */
uint8_t ov5640_set_output_size(uint16_t width, uint16_t height);                                        /* 设置OV5640模块输出图像尺寸 */
uint8_t ov5640_get_frame(uint32_t dts_addr, ov5640_get_type_t type, void (*before_transfer)(void)); /* 获取OV5640模块摄像头一帧图像数据 */

#endif
