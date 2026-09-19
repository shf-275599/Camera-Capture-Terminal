/**
 * @file    app_sw_jpeg.h
 * @brief   CM7 软件 JPEG 解码应用接口
 *
 * 本模块使用 TJpgDec 完成 JPEG 解码：
 * - 输入端从调用者提供的 JPEG 字节数组中读取压缩数据；
 * - 解码器以 RGB565 MCU 像素块的形式调用内部输出回调；
 * - 输出回调将像素块写入 LTDC 使用的 SDRAM 帧缓冲，并清理 D-Cache。
 *
 * 本头文件只暴露解码入口，TJpgDec 的输入/输出回调、工作内存池和
 * 解码上下文均为本文件私有实现，调用者不需要了解其内部数据结构。
 */
#ifndef APP_SW_JPEG_H
#define APP_SW_JPEG_H

#include <stdint.h>

/**
 * @brief  解码 JPEG 数据并写入 LCD 帧缓冲
 * @param  jpeg_data      JPEG 压缩数据首地址
 * @param  jpeg_data_size JPEG 压缩数据长度，单位为字节
 * @param  x              解码图像左上角的 LCD X 坐标
 * @param  y              解码图像左上角的 LCD Y 坐标
 * @return None
 *
 * 函数会先解析 JPEG 头部并获取原图尺寸，再以 scale=0（原始尺寸）
 * 执行解码。若 JPEG 解析失败、图像尺寸超过 LCD 或输出区域越界，
 * 函数会提前返回或终止解码，并通过调试串口输出状态信息。
 *
 * @warning
 * jpeg_data 指向的缓冲区在函数返回前必须保持有效。当前实现不会复制
 * 整个 JPEG 文件，而是由 TJpgDec 通过输入回调按需读取该缓冲区。
 */
void jpeg_sw_decode(uint8_t *jpeg_data, unsigned int jpeg_data_size,
                    uint16_t x, uint16_t y);

#endif /* APP_SW_JPEG_H */
