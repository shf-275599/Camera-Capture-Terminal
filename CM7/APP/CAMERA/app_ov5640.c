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
uint8_t app_ov5640_run_test(void)
{
    uint8_t ret;
    volatile uint16_t *frame = (volatile uint16_t *)LTDC_FRAME_BUF_ADDR;
    uint16_t green = frame[800U * 240U + 350U];
    uint32_t green_mismatches = 0;
    uint32_t x;

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
    /* 打印色条采样值，并以中心行 x=350 的像素检查 x=310~389 区间的一致性。 */
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
    green = frame[800U * 240U + 350U];
    for (x = 310U; x < 390U; x++)
    {
        if (frame[800U * 240U + x] != green)
        {
            green_mismatches++;
        }
    }
    LOG_INFO("CM7", "COLORBAR green=%04X mismatches=%lu/80", green, (unsigned long)green_mismatches);

    /* 关闭测试图案，等待画面稳定后采集实际摄像头图像。 */
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
