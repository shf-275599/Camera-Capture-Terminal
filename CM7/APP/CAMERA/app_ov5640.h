/**
 * @file    app_ov5640.h
 * @brief   OV5640摄像头简单初始化测试
 */
#ifndef __APP_OV5640_H
#define __APP_OV5640_H

#include <stdint.h>

void app_ov5640_init(void);
void app_ov5640_capture_colorbar(void);
uint8_t app_ov5640_run(void);

#endif /* __APP_OV5640_H */
