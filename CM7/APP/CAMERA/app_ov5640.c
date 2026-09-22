/**
 * @file    app_ov5640.c
 * @brief   OV5640摄像头简单初始化测试
 */
#include "app_ov5640.h"
#include "ov5640.h"
#include <stdio.h>

void app_ov5640_init(void)
{
    uint8_t ret;

    printf("CM7: OV5640 init start\r\n");

    ret = ov5640_init();
    if (ret == OV5640_EOK)
    {
        printf("CM7: OV5640 init OK, chip ID verified\r\n");
    }
    else
    {
        printf("CM7: OV5640 init FAILED (err=%d)\r\n", ret);
    }
}
