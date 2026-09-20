#include "app_hw_jpeg.h"
#include <string.h>
#include <stdio.h>
#include "jpeg.h"
#include "jpeg_utils.h"

#define JPEG_CHUNK_SIZE (128U * 1024U) // JPEG DMA 单次输入数据块大小
uint8_t *jpeg_in = NULL;              // JPEG 压缩码流缓冲区
uint8_t *jpeg_mcu = NULL;             // JPEG 解码输出缓冲区，用于接收外设输出的 YCbCr MCU 数据
uint32_t jpeg_in_size = 0;            // 当前 JPEG 压缩码流的总字节数

typedef struct
{
  volatile uint8_t active;    // 会话是否仍在进行
  volatile uint8_t done;      // 完成解码后置位
  volatile uint8_t error;     // 检测到外设或 DMA 错误后置位
  volatile uint32_t in_off;   // 输入码流偏移量
  volatile uint32_t in_size;  // 输入码流总长度
  volatile uint32_t out_size; // MCU 数据字节数
  JPEG_ConfTypeDef info;      // JPEG 头解析得到的图像配置，如尺寸、颜色分量和采样信息
} jpeg_session_t;

static jpeg_session_t g_jpeg; // 当前 JPEG 解码会话

/**
 * @brief JPEG 解码函数
 *
 * @param in 输入 JPEG 压缩码流指针
 * @param in_size 输入 JPEG 压缩码流长度
 * @param mcu_out 输出 MCU 数据指针
 * @return int 0 成功，-1 失败
 */
int jpeg_hw_decode(uint8_t *in, uint32_t in_size, uint8_t *mcu_out)
{
  uint32_t first_in;                  // 首个输入块长度
  memset(&g_jpeg, 0, sizeof(g_jpeg)); // 清除上一帧会话状态、计数和 JPEG 配置信息。
  g_jpeg.in_size = in_size;           // 保存码流总长度
  g_jpeg.active = 1;                  // 标记会话已开始
  jpeg_in = in;                       // 保存输入指针
  jpeg_mcu = mcu_out;                 // 保存输出指针
  jpeg_in_size = in_size;             // 保存码流总长度

  first_in = (in_size > JPEG_CHUNK_SIZE) ? JPEG_CHUNK_SIZE : in_size; // 首块不能超过 DMA 输入块容量或实际码流长度

  // 只提交首块；后续输入由 HAL_JPEG_GetDataCallback() 配置，形成 DMA 输入滑动窗口
  if (HAL_JPEG_Decode_DMA(&hjpeg, in, first_in, mcu_out, JPEG_CHUNK_SIZE) != HAL_OK)
  {
    g_jpeg.active = 0; // DMA 或 JPEG 外设启动失败，立即结束本次会话
    return -1;
  }

  uint32_t t0 = HAL_GetTick(); // 记录等待起始时间，避免异常情况下永久阻塞

  // 等待完成、错误或 5 秒超时；__WFI() 让处理器等待 DMA、JPEG 或定时器中断唤醒
  while (!g_jpeg.done && !g_jpeg.error &&
         (HAL_GetTick() - t0) < 5000U)
  {
    __WFI();
  }

  g_jpeg.active = 0; // 成功、失败或超时后均关闭会话，避免后续回调继续归属本次操作

  if (!g_jpeg.done || g_jpeg.error)
  {
    HAL_JPEG_Abort(&hjpeg); // 超时或错误时终止外设，释放忙状态，避免影响下一次解码
    return -1;
  }

  return 0;
}

/**
 * @brief JPEG 头信息就绪回调
 *
 * @param hjpeg JPEG 外设句柄
 * @param info 解码后的 JPEG 头信息就绪时被触发，用于通知应用层解码完成并提供 JPEG 头信息。
 */
void HAL_JPEG_InfoReadyCallback(JPEG_HandleTypeDef *hjpeg,
                                JPEG_ConfTypeDef *info)
{
  if (g_jpeg.active)
  {
    g_jpeg.info = *info;
  }
}

/**
 * @brief JPEG 输入数据回调
 *
 * @param hjpeg JPEG 外设句柄
 * @param length 已消耗的输入数据长度
 * @note 该回调在解码过程中被触发，用于通知应用层解码完成并提供 MCU 数据。
 */
void HAL_JPEG_GetDataCallback(JPEG_HandleTypeDef *hjpeg, uint32_t length)
{
  uint32_t remain, next;

  if (!g_jpeg.active)
    return;

  g_jpeg.in_off += length;
  remain = g_jpeg.in_size - g_jpeg.in_off;

  if (remain == 0U)
  {
    HAL_JPEG_ConfigInputBuffer(hjpeg, jpeg_in + g_jpeg.in_off, 0U);
    return;
  }

  next = (remain > JPEG_CHUNK_SIZE) ? JPEG_CHUNK_SIZE : remain;
  HAL_JPEG_ConfigInputBuffer(hjpeg, jpeg_in + g_jpeg.in_off, next);
}

/**
 * @brief JPEG 数据就绪回调
 *
 * @param hjpeg JPEG 外设句柄
 * @param data 解码后的 MCU 数据指针
 * @param length 解码后的 MCU 数据长度
 * @note 该回调在解码完成后被触发，用于通知应用层解码完成并提供 MCU 数据。
 */
void HAL_JPEG_DataReadyCallback(JPEG_HandleTypeDef *hjpeg,
                                uint8_t *data, uint32_t length)
{
  uint32_t remain, next;

  if (!g_jpeg.active)
    return;

  g_jpeg.out_size += length;
  remain = JPEG_CHUNK_SIZE - g_jpeg.out_size; // MCU 缓冲剩余容量
  if (remain == 0U)
  {
    g_jpeg.error = 1U;
    HAL_JPEG_Pause(hjpeg, JPEG_PAUSE_RESUME_OUTPUT);
    return;
  }

  next = (remain > JPEG_CHUNK_SIZE) ? JPEG_CHUNK_SIZE : remain;
  HAL_JPEG_ConfigOutputBuffer(hjpeg, data + length, next);
}

/**
 * @brief JPEG 解码完成回调
 *
 * @param hjpeg JPEG 外设句柄
 * @note 该回调在解码完成后被触发，用于通知应用层解码完成。
 */
void HAL_JPEG_DecodeCpltCallback(JPEG_HandleTypeDef *hjpeg)
{
  if (g_jpeg.active)
    g_jpeg.done = 1U;
}

/**
 * @brief JPEG 错误回调
 *
 * @param hjpeg JPEG 外设句柄
 * @note 该回调在解码过程中检测到外设或 DMA 错误时被触发，用于通知应用层错误。
 */
void HAL_JPEG_ErrorCallback(JPEG_HandleTypeDef *hjpeg)
{
  if (g_jpeg.active)
    g_jpeg.error = 1U;
}