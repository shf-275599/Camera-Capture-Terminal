#include "app_hw_jpeg.h"
#include <string.h>
#include <stdio.h>
#include "jpeg.h"
#include "jpeg_utils.h"
#include "ltdc.h"
#include "lcd.h"

#define JPEG_MCU_BUFFER_ADDR 0xD0100000U
#define JPEG_RGB_BUFFER_ADDR 0xD0110000U
#define JPEG_MCU_BUFFER_SIZE (32U * 1024U)
#define JPEG_RGB_BUFFER_SIZE (800U * 480U * 2U)

#define JPEG_INPUT_CHUNK_SIZE (32U * 1024U)   // JPEG DMA 单次输入数据块大小
#define JPEG_OUTPUT_BUFFER_SIZE (32U * 1024U) // JPEG 解码输出缓冲区大小

#define JPEG_MCU_BUFFER \
  ((uint8_t *)JPEG_MCU_BUFFER_ADDR) // MCU 数据缓冲区指针

#define JPEG_RGB_BUFFER \
  ((uint8_t *)JPEG_RGB_BUFFER_ADDR) // RGB 数据缓冲区指针

typedef struct
{
  volatile uint8_t active;     // 会话是否仍在进行
  volatile uint8_t info_ready; // JPEG 头解析完成后置位
  volatile uint8_t done;       // 完成解码后置位
  volatile uint8_t error;      // 检测到外设或 DMA 错误后置位
  volatile uint32_t in_off;    // 输入码流偏移量
  uint32_t in_size;            // 输入码流总长度
  uint32_t out_size;           // MCU 数据字节数
  JPEG_ConfTypeDef info;       // JPEG 头解析得到的图像配置，如尺寸、颜色分量和采样信息
} jpeg_session_t;

static jpeg_session_t g_jpeg;              // 当前 JPEG 解码会话
static const uint8_t *g_jpeg_input = NULL; // 当前 JPEG 解码会话的输入指针
/**
 * @brief JPEG 解码函数
 *
 * @param in 输入 JPEG 压缩码流指针
 * @param in_size 输入 JPEG 压缩码流长度
 * @param mcu_out 输出 MCU 数据指针
 * @return int 0 成功，-1 失败
 */
int jpeg_hw_decode(const uint8_t *data, uint32_t size, uint16_t x, uint16_t y)
{
  uint32_t first_input;
  uint32_t start_tick;

  if ((data == NULL) || (size < 4U))
  {
    return -1;
  }

  if ((data[0] != 0xFFU) || (data[1] != 0xD8U))
  {
    return -1;
  }

  if (g_jpeg.active)
  {
    return -1;
  }

  memset(&g_jpeg, 0, sizeof(g_jpeg));

  g_jpeg_input = data;
  g_jpeg.in_size = size;
  g_jpeg.active = 1U;

  first_input = size;
  if (first_input > JPEG_INPUT_CHUNK_SIZE)
  {
    first_input = JPEG_INPUT_CHUNK_SIZE;
  }

  /*
   * 输出位于 Cacheable SDRAM。
   * 防止已有脏 Cache Line 在 DMA 完成后覆盖 MDMA 写入的数据。
   */
  jpeg_cache_clean(JPEG_MCU_BUFFER,
                   JPEG_OUTPUT_BUFFER_SIZE);
  jpeg_cache_invalidate(JPEG_MCU_BUFFER,
                        JPEG_OUTPUT_BUFFER_SIZE);

  if (HAL_JPEG_Decode_DMA(&hjpeg,
                          (uint8_t *)data,
                          first_input,
                          JPEG_MCU_BUFFER,
                          JPEG_OUTPUT_BUFFER_SIZE) != HAL_OK)
  {
    g_jpeg.active = 0U;
    return -1;
  }

  start_tick = HAL_GetTick();

  while (!g_jpeg.done &&
         !g_jpeg.error &&
         ((HAL_GetTick() - start_tick) < 5000U))
  {
    __WFI();
  }

  if (!g_jpeg.done || g_jpeg.error)
  {
    HAL_JPEG_Abort(&hjpeg);
    g_jpeg.active = 0U;

    printf("CM7: HW JPEG failed, error=0x%08lx\n",
           HAL_JPEG_GetError(&hjpeg));
    return -1;
  }

  jpeg_cache_invalidate(JPEG_MCU_BUFFER,
                        g_jpeg.out_size);

  if (jpeg_convert_to_rgb565() != 0)
  {
    g_jpeg.active = 0U;
    return -1;
  }

  if (jpeg_copy_rgb565_to_lcd(x, y) != 0)
  {
    g_jpeg.active = 0U;
    return -1;
  }

  printf("CM7: HW JPEG %lux%lu, MCU=%lu bytes\n",
         g_jpeg.info.ImageWidth,
         g_jpeg.info.ImageHeight,
         g_jpeg.out_size);

  g_jpeg.active = 0U;
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
  (void)hjpeg;

  if (!g_jpeg.active)
  {
    return;
  }

  g_jpeg.info = *info;
  g_jpeg.info_ready = 1U;
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
  uint32_t remaining;
  uint32_t next_length;

  if (!g_jpeg.active)
  {
    return;
  }

  if (length > g_jpeg.in_size - g_jpeg.in_off)
  {
    g_jpeg.error = 1U;
    HAL_JPEG_Pause(hjpeg, JPEG_PAUSE_RESUME_INPUT);
    return;
  }

  g_jpeg.in_off += length;
  remaining = g_jpeg.in_size - g_jpeg.in_off;

  if (remaining == 0U)
  {
    HAL_JPEG_ConfigInputBuffer(
        hjpeg,
        (uint8_t *)g_jpeg_input + g_jpeg.in_off,
        0U);
    return;
  }

  next_length = remaining;
  if (next_length > JPEG_INPUT_CHUNK_SIZE)
  {
    next_length = JPEG_INPUT_CHUNK_SIZE;
  }

  HAL_JPEG_ConfigInputBuffer(
      hjpeg,
      (uint8_t *)g_jpeg_input + g_jpeg.in_off,
      next_length);
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
  (void)hjpeg;
  (void)data;

  if (!g_jpeg.active)
  {
    return;
  }

  if (length > JPEG_OUTPUT_BUFFER_SIZE - g_jpeg.out_size)
  {
    g_jpeg.error = 1U;
    return;
  }

  g_jpeg.out_size += length;
}

/**
 * @brief JPEG 解码完成回调
 *
 * @param hjpeg JPEG 外设句柄
 * @note 该回调在解码完成后被触发，用于通知应用层解码完成。
 */
void HAL_JPEG_DecodeCpltCallback(JPEG_HandleTypeDef *hjpeg)
{
  (void)hjpeg;

  if (g_jpeg.active)
  {
    g_jpeg.done = 1U;
  }
}

/**
 * @brief JPEG 错误回调
 *
 * @param hjpeg JPEG 外设句柄
 * @note 该回调在解码过程中检测到外设或 DMA 错误时被触发，用于通知应用层错误。
 */
void HAL_JPEG_ErrorCallback(JPEG_HandleTypeDef *hjpeg)
{
  (void)hjpeg;

  if (g_jpeg.active)
  {
    g_jpeg.error = 1U;
  }
}

/**
 * @brief 清除缓存
 *
 * @param address 缓存起始地址
 * @param length 缓存长度
 * @note 该函数用于清除指定地址范围的缓存，确保数据从主内存刷新到缓存中。
 */
static void jpeg_cache_clean(const void *address, uint32_t length)
{
  uintptr_t start;
  uintptr_t end;

  start = (uintptr_t)address & ~(uintptr_t)31U;
  end = ((uintptr_t)address + length + 31U) & ~(uintptr_t)31U;

  SCB_CleanDCache_by_Addr((uint32_t *)start,
                          (int32_t)(end - start));
  __DSB();
}

/**
 * @brief 使缓缓存
 *
 * @param address 缓存起始地址
 * @param length 缓存长度
 * @note 该函数用于使缓指定地址范围的缓存，确保数据从缓存刷新到主内存。
 */
static void jpeg_cache_invalidate(void *address, uint32_t length)
{
  uintptr_t start;
  uintptr_t end;

  start = (uintptr_t)address & ~(uintptr_t)31U;
  end = ((uintptr_t)address + length + 31U) & ~(uintptr_t)31U;

  SCB_InvalidateDCache_by_Addr((uint32_t *)start,
                               (int32_t)(end - start));
  __DSB();
}

/**
 * @brief 将解码后的 MCU 数据转换为 RGB565 格式
 *
 * @note 该函数用于将解码后的 MCU 数据转换为 RGB565 格式，确保数据从缓存刷新到主内存。
 * @return int 0 成功，-1 失败
 */
static int jpeg_convert_to_rgb565(void)
{
  JPEG_YCbCrToRGB_Convert_Function convert;
  uint32_t image_mcu_count;
  uint32_t converted_bytes;
  uint32_t converted_mcus;

  if (!g_jpeg.info_ready)
  {
    return -1;
  }

  if ((g_jpeg.info.ImageWidth > 800U) ||
      (g_jpeg.info.ImageHeight > 480U))
  {
    return -1;
  }

  if ((g_jpeg.info.ImageWidth *
       g_jpeg.info.ImageHeight * 2U) >
      JPEG_RGB_BUFFER_SIZE)
  {
    return -1;
  }

  if (JPEG_GetDecodeColorConvertFunc(&g_jpeg.info,
                                     &convert,
                                     &image_mcu_count) != HAL_OK)
  {
    return -1;
  }

  converted_bytes = 0U;

  converted_mcus = convert(JPEG_MCU_BUFFER,
                           JPEG_RGB_BUFFER,
                           0U,
                           g_jpeg.out_size,
                           &converted_bytes);

  if (converted_mcus != image_mcu_count)
  {
    return -1;
  }

  return 0;
}

/**
 * @brief 将 RGB565 格式的图像复制到 LCD 显示区域
 *
 * @param x LCD 显示区域的起始 X 坐标
 * @param y LCD 显示区域的起始 Y 坐标
 * @note 该函数用于将 RGB565 格式的图像复制到 LCD 显示区域，确保数据从缓存刷新到主内存。
 * @return int 0 成功，-1 失败
 */
static int jpeg_copy_rgb565_to_lcd(uint16_t x, uint16_t y)
{
  uint32_t width;
  uint32_t height;
  uint32_t row;

  width = g_jpeg.info.ImageWidth;
  height = g_jpeg.info.ImageHeight;

  if ((x + width > lcddev.width) ||
      (y + height > lcddev.height))
  {
    return -1;
  }

  for (row = 0U; row < height; row++)
  {
    const uint16_t *source;
    uint16_t *destination;

    source = (const uint16_t *)JPEG_RGB_BUFFER +
             row * width;

    destination = (uint16_t *)LTDC_FRAME_BUF_ADDR +
                  (y + row) * lcddev.width +
                  x;

    memcpy(destination,
           source,
           width * sizeof(uint16_t));

    jpeg_cache_clean(destination,
                     width * sizeof(uint16_t));
  }

  __DSB();
  return 0;
}