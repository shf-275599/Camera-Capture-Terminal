/**
 * @file    app_ov5640.c
 * @brief   OV5640摄像头简单初始化测试
 */
#include "app_ov5640.h"
#include "ov5640.h"
#include "lcd_ltdc.h"
#include "dcmi.h"
#include "ov5640_dcmi.h"
#include "cmsis_os2.h"
#include "mylog.h"

/**
 * @brief  初始化 OV5640，检查芯片并切换一次摄像头 LED
 */
void app_ov5640_init(void)
{
    uint8_t ret;

    LOG_INFO("CM7","OV5640 init start");

    ret = ov5640_init();
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7","OV5640 init FAILED (err=%d)", ret);
        return;
    }

    ov5640_led_on();
    ov5640_led_off();
    
    LOG_INFO("CM7","OV5640 init OK, chip ID verified");
}

/**
 * @brief  配置 OV5640 输出 800x480 RGB565 色条并采集到 LTDC 帧缓冲
 */
void app_ov5640_capture_colorbar(void)
{
    uint8_t ret = 0;

    LOG_INFO("CM7","OV5640 capture colorbar start");
    /* 1. 设置输出格式为 RGB565 */
    if (ov5640_set_output_format(OV5640_OUTPUT_FORMAT_RGB565) != OV5640_EOK)
    {
        LOG_ERROR("CM7","OV5640 set output format failed (err=%d)", ret);
        return;
    }
    /* 2. 设置输出尺寸为 800x480 */
    if (ov5640_set_output_size(800, 480) != OV5640_EOK)
    {
        LOG_ERROR("CM7","OV5640 set output size failed (err=%d)", ret);
        return;
    }
    /* 3. 设置测试模式为 颜色条测试 */
    if (ov5640_set_test_pattern(OV5640_TEST_PATTERN_COLOR_BAR) != OV5640_EOK)
    {
        LOG_ERROR("CM7","OV5640 set test pattern failed (err=%d)", ret);
        return;
    }
    /* 4. 开始捕获 */
    if (ov5640_get_frame(LTDC_FRAME_BUF_ADDR,         // 0xD0000000
                         OV5640_GET_TYPE_DTS_16B_INC, // RGB565 = 2字节/像素
                         NULL) != OV5640_EOK)         // 帧数据传输前无需要完成的工作
    {
        LOG_ERROR("CM7","OV5640 get frame failed (err=%d)", ret);
        return;
    }
    LOG_INFO("CM7","Colorbar frame captured to 0x%08X", LTDC_FRAME_BUF_ADDR);
}

/**
 * @brief  采集色条和真实画面并打印像素信息，随后持续采集真实画面
 * @return 配置或采集失败时返回 OV5640 错误码；正常运行时持续采集，不返回
 */
uint8_t app_ov5640_run(void)
{
    uint8_t ret;

    ret = ov5640_set_output_format(OV5640_OUTPUT_FORMAT_RGB565);
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7", "ov5640_set_output_format failed (err=%d)", ret);
        return ret;
    }
    ret = ov5640_set_output_size(800, 480);
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7", "ov5640_set_output_size failed (err=%d)", ret);
        return ret;
    }
    ret = ov5640_set_light_mode(OV5640_LIGHT_MODE_ADVANCED_AWB);
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7", "ov5640_set_light_mode failed (err=%d)", ret);
        return ret;
    }
    ret = ov5640_set_color_saturation(OV5640_COLOR_SATURATION_3);
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7", "ov5640_set_color_saturation failed (err=%d)", ret);
        return ret;
    }
    ret = ov5640_set_brightness(OV5640_BRIGHTNESS_3);
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7", "ov5640_set_brightness failed (err=%d)", ret);
        return ret;
    }
    ret = ov5640_set_contrast(OV5640_CONTRAST_2);
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7", "ov5640_set_contrast failed (err=%d)", ret);
        return ret;
    }
    ret = ov5640_set_hue(OV5640_HUE_6);
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7", "ov5640_set_hue failed (err=%d)", ret);
        return ret;
    }
    ret = ov5640_set_exposure_level(OV5640_EXPOSURE_LEVEL_2);
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7", "ov5640_set_exposure_level failed (err=%d)", ret);
        return ret;
    }
    ret = ov5640_set_sharpness_level(OV5640_SHARPNESS_5);
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7", "ov5640_set_sharpness_level failed (err=%d)", ret);
        return ret;
    }

    /* 三帧 SDRAM：显示帧、采集帧、待显示帧。DMA 持续接收，LTDC 在消隐期换帧。 */
    /* 避开从 0xD0110000 开始的 JPEG 工作缓冲，采集帧放在独立区域。 */
    LOG_INFO("CM7", "CAMERA_STREAM_START");
    ret = ov5640_dcmi_stream_start(LTDC_FRAME_BUF_ADDR, 0xD0300000U, 800U * 480U * 2U);
    if (ret != OV5640_DCMI_EOK)
    {
        LOG_ERROR("CM7", "CAMERA_STREAM_START_FAIL=%u", ret);
        return ret;
    }
    for (;;)
    {
        ret = ov5640_dcmi_stream_poll();
        if (ret != OV5640_DCMI_EOK)
        {
            ov5640_dcmi_stream_stop();
            ov5640_dcmi_stream_log_error();
            LOG_ERROR("CM7", "CAMERA_STREAM_FAIL=%u DCMI=0x%lX DMA=0x%lX",
                      ret, (unsigned long)hdcmi.ErrorCode,
                      (unsigned long)hdcmi.DMA_Handle->ErrorCode);
            return ret;
        }
        osDelay(1);
    }
}
