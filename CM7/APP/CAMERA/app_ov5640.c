/**
 * @file    app_ov5640.c
 * @brief   OV5640摄像头简单初始化测试
 */
#include "app_ov5640.h"
#include "ov5640.h"
#include "lcd_ltdc.h"
#include <stdio.h>

void app_ov5640_init(void)
{
    uint8_t ret;

    printf("CM7: OV5640 init start\r\n");

    ret = ov5640_init();
    if (ret != OV5640_EOK)
    {
        printf("CM7: OV5640 init FAILED (err=%d)\r\n", ret);
        
    }

    ov5640_led_on();
    ov5640_led_off();
    
    printf("CM7: OV5640 init OK, chip ID verified\r\n");
}

void app_ov5640_capture_colorbar(void)
{
    uint8_t ret = 0;

    printf("CM7: OV5640 capture colorbar start\r\n");
    /* 1. 设置输出格式为 RGB565 */
    if (ov5640_set_output_format(OV5640_OUTPUT_FORMAT_RGB565) != OV5640_EOK)
    {
        printf("CM7: OV5640 set output format failed (err=%d)\r\n", ret);
        return;
    }
    /* 2. 设置输出尺寸为 800x480 */
    if (ov5640_set_output_size(800, 480) != OV5640_EOK)
    {
        printf("CM7: OV5640 set output size failed (err=%d)\r\n", ret);
        return;
    }
    /* 3. 设置测试模式为 颜色条测试 */
    if (ov5640_set_test_pattern(OV5640_TEST_PATTERN_COLOR_BAR) != OV5640_EOK)
    {
        printf("CM7: OV5640 set test pattern failed (err=%d)\r\n", ret);
        return;
    }
    /* 4. 开始捕获 */
    if (ov5640_get_frame(LTDC_FRAME_BUF_ADDR,         // 0xD0000000
                         OV5640_GET_TYPE_DTS_16B_INC, // RGB565 = 2字节/像素
                         NULL) != OV5640_EOK)         // 帧数据传输前无需要完成的工作
    {
        printf("CM7: OV5640 get frame failed (err=%d)\r\n", ret);
        return;
    }
    printf("CM7: Colorbar frame captured to 0x%08X\r\n", LTDC_FRAME_BUF_ADDR);
}