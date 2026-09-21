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

/* 错误代码 */
#define OV5640_DCMI_EOK     0   /* 没有错误 */
#define OV5640_DCMI_ERROR   1   /* 错误 */
#define OV5640_DCMI_EINVAL  2   /* 非法参数 */

/* 操作函数 */
void ov5640_dcmi_start(uint32_t dts_addr, uint32_t meminc, uint32_t memdataalignment, uint32_t len);    /* 开始OV5640模块DCMI接口DMA传输 */

#endif

