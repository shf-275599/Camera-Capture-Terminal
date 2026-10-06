/**
 * @file    app_ov5640.c
 * @brief   OV5640摄像头简单初始化测试
 */
#include "app_ov5640.h"
#include "ov5640.h"
#include "lcd_ltdc.h"
#include "dcmi.h"
#include "cmsis_os2.h"
#include "mylog.h"

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

uint8_t app_ov5640_run_test(void)
{
    uint8_t ret;

    LOG_INFO("CM7", "CAMERA_TEST_COLORBAR_START");
    ret = ov5640_set_output_format(OV5640_OUTPUT_FORMAT_RGB565);
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7", "CAMERA_TEST_COLORBAR_FAIL format=%u", ret);
        return ret;
    }
    ret = ov5640_set_output_size(800, 480);
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7", "CAMERA_TEST_COLORBAR_FAIL size=%u", ret);
        return ret;
    }
    ret = ov5640_set_test_pattern(OV5640_TEST_PATTERN_COLOR_BAR);
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7", "CAMERA_TEST_COLORBAR_FAIL pattern=%u", ret);
        return ret;
    }
    ret = ov5640_get_frame(LTDC_FRAME_BUF_ADDR, OV5640_GET_TYPE_DTS_16B_INC, NULL);
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7", "CAMERA_TEST_COLORBAR_FAIL capture=%u", ret);
        return ret;
    }
    LOG_INFO("CM7", "CAMERA_TEST_COLORBAR_PASS");
    LOG_INFO("CM7", "COLORBAR pixels: %04X %04X %04X %04X",
             ((volatile uint16_t *)LTDC_FRAME_BUF_ADDR)[0],
             ((volatile uint16_t *)LTDC_FRAME_BUF_ADDR)[400],
             ((volatile uint16_t *)LTDC_FRAME_BUF_ADDR)[800 * 240],
             ((volatile uint16_t *)LTDC_FRAME_BUF_ADDR)[800 * 479]);
    LOG_INFO("CM7", "COLORBAR center: %04X %04X %04X %04X %04X %04X %04X %04X",
             ((volatile uint16_t *)LTDC_FRAME_BUF_ADDR)[800 * 240 + 50],
             ((volatile uint16_t *)LTDC_FRAME_BUF_ADDR)[800 * 240 + 150],
             ((volatile uint16_t *)LTDC_FRAME_BUF_ADDR)[800 * 240 + 250],
             ((volatile uint16_t *)LTDC_FRAME_BUF_ADDR)[800 * 240 + 350],
             ((volatile uint16_t *)LTDC_FRAME_BUF_ADDR)[800 * 240 + 450],
             ((volatile uint16_t *)LTDC_FRAME_BUF_ADDR)[800 * 240 + 550],
             ((volatile uint16_t *)LTDC_FRAME_BUF_ADDR)[800 * 240 + 650],
             ((volatile uint16_t *)LTDC_FRAME_BUF_ADDR)[800 * 240 + 750]);

    LOG_INFO("CM7", "CAMERA_TEST_REAL_START");
    ret = ov5640_set_test_pattern(OV5640_TEST_PATTERN_OFF);
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7", "CAMERA_TEST_REAL_FAIL pattern=%u", ret);
        return ret;
    }
    osDelay(2000);
    ret = ov5640_get_frame(LTDC_FRAME_BUF_ADDR, OV5640_GET_TYPE_DTS_16B_INC, NULL);
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7", "CAMERA_TEST_REAL_FAIL capture=%u", ret);
        return ret;
    }
    LOG_INFO("CM7", "CAMERA_TEST_REAL_PASS");
    LOG_INFO("CM7", "REAL pixels: %04X %04X %04X %04X",
             ((volatile uint16_t *)LTDC_FRAME_BUF_ADDR)[0],
             ((volatile uint16_t *)LTDC_FRAME_BUF_ADDR)[400],
             ((volatile uint16_t *)LTDC_FRAME_BUF_ADDR)[800 * 240],
             ((volatile uint16_t *)LTDC_FRAME_BUF_ADDR)[800 * 479]);
    LOG_INFO("CM7", "CAMERA_TEST_PASS");

    return OV5640_EOK;
}
