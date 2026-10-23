/**
 ****************************************************************************************************
 * @file        ov5640_dcmi.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2022-06-21
 * @brief       OV5640模块DCMI接口驱动代码
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

#ifndef __OV5640_DCMI_H
#define __OV5640_DCMI_H

#include "ov5640.h"

/* DCMI传输错误码 */
#define OV5640_DCMI_EOK 0      /* 没有错误 */
#define OV5640_DCMI_ERROR 1    /* 错误 */
#define OV5640_DCMI_EINVAL 2   /* 非法参数 */
#define OV5640_DCMI_ETIMEOUT 3 /* 超时 */

/* 启动一次快照模式的DCMI DMA传输，并等待帧完成中断。 */
uint8_t ov5640_dcmi_start(uint32_t dts_addr, uint32_t meminc, uint32_t memdataalignment, uint32_t len); /* 开始OV5640模块DCMI接口DMA传输 */

/* 800x480 RGB565 连续流：显示帧和两块独立的采集帧。 */
uint8_t ov5640_dcmi_stream_start(uint32_t display_addr, uint32_t capture_addr, uint32_t frame_size);
uint8_t ov5640_dcmi_stream_poll(void);
void ov5640_dcmi_stream_stop(void);
void ov5640_dcmi_stream_log_error(void);
/* 在 HAL 清标志和中止 DMA 前保存连续流故障现场。 */
void ov5640_dcmi_stream_irq_snapshot(void);
/* 复制完整显示帧；由任务调用，复制期间暂缓换帧，采集不中断。 */
int ov5640_dcmi_copy_frame(void *destination);
/* 锁定完整显示帧供 CPU 只读，调用者必须在停止访问后释放；采集继续。 */
int ov5640_dcmi_acquire_frame(uint8_t **frame);
void ov5640_dcmi_release_frame(void);

#endif
