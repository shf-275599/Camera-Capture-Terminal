/**
 * @file    camera_test.c
 * @brief   OV5640摄像头简单初始化测试
 */
#include "camera_test.h"
#include "ov5640.h"
#include <stdio.h>

void camera_init_test(void)
{
    uint8_t ret;

    printf("CM7: OV5640 camera init test start\r\n");

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
