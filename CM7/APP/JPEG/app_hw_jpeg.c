/**
 * @file    app_hw_jpeg.c
 * @brief   使用 STM32H7 JPEG Codec 和 MDMA 解码 JPEG，并将 RGB565 图像写入 LCD 帧缓冲
 *
 * 数据流如下：
 * JPEG 压缩数据 -> SDRAM 输入缓冲 -> MDMA -> JPEG Codec
 * -> YCbCr MCU 缓冲（SDRAM） -> jpeg_utils -> RGB565 缓冲（SDRAM）
 * -> LTDC 帧缓冲（SDRAM） -> LCD。
 *
 * JPEG Codec 负责解析码流并输出按 MCU 排列的 YCbCr 数据，jpeg_utils 根据
 * JPEG 头中的颜色空间和色度采样方式将其转换为 RGB565。由于输入、MCU、
 * RGB 和 LTDC 帧缓冲均位于可缓存 SDRAM，CPU 与 MDMA/LTDC 切换缓冲所有权
 * 时必须执行相应的 D-Cache Clean 或 Invalidate 操作。
 *
 * 当前实现使用固定 SDRAM 地址和单个 32 KiB MCU 输出缓冲，适合输出数据可
 * 完整放入该缓冲的图片。更大图片需要改为分块输出或双缓冲流水处理。
 */
#include "app_hw_jpeg.h"
#include <string.h>
#include <stdio.h>
#include "jpeg.h"
#include "jpeg_utils.h"
#include "ltdc.h"
#include "lcd.h"
#include "mylog.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * SDRAM 缓冲布局。地址避开从 0xD0000000 开始的 800 x 480 RGB565
 * LTDC 帧缓冲，各区域之间也不重叠。
 */
#define JPEG_MCU_BUFFER_ADDR 0xD0100000U
#define JPEG_RGB_BUFFER_ADDR 0xD0110000U
#define JPEG_INPUT_BUFFER_ADDR 0xD01E0000U
#define JPEG_MCU_BUFFER_SIZE (32U * 1024U)
#define JPEG_RGB_BUFFER_SIZE (800U * 480U * 2U)

/* JPEG 输入和输出 MDMA 单次可访问的最大缓冲容量。 */
#define JPEG_INPUT_CHUNK_SIZE (32U * 1024U)
#define JPEG_OUTPUT_BUFFER_SIZE (32U * 1024U)
/* JPEG FIFO 阈值请求对应 32 字节 MDMA BufferTransferLength。 */
#define JPEG_MDMA_TRANSFER_UNIT 32U

/* JPEG Codec 通过 MDMA 将解码后的 YCbCr MCU 数据写入该缓冲。 */
#define JPEG_MCU_BUFFER \
  ((uint8_t *)JPEG_MCU_BUFFER_ADDR)

/* jpeg_utils 将 MCU 数据转换为按图片宽度紧密排列的 RGB565 像素。 */
#define JPEG_RGB_BUFFER \
  ((uint8_t *)JPEG_RGB_BUFFER_ADDR)

/* 原始码流复制到该缓冲并按 MDMA 传输单元补齐后交给 JPEG Codec。 */
#define JPEG_INPUT_BUFFER \
  ((uint8_t *)JPEG_INPUT_BUFFER_ADDR)

/**
 * @brief 一次硬件 JPEG 解码会话的异步状态
 *
 * JPEG 和 MDMA 中断回调通过该结构与同步等待流程交换状态。active 用于
 * 拒绝并发解码，并防止会话结束后到达的回调修改下一次解码状态。
 */
typedef struct
{
  volatile uint8_t active;     // 会话是否仍在进行
  volatile uint8_t info_ready; // JPEG 头解析完成后置位
  volatile uint8_t done;       // 完成解码后置位
  volatile uint8_t error;      // 检测到外设或 MDMA 错误后置位 
  volatile uint32_t in_off;    // 已被 JPEG Codec 消耗的输入字节数 
  uint32_t in_size;            // 按 MDMA 传输单元补齐后的输入长度 
  uint32_t out_size;           // JPEG Codec 实际输出的 MCU 数据字节数
  uint32_t rgb_stride;         // MCU 对齐后的 RGB 缓冲行宽，单位为像素
  JPEG_ConfTypeDef info;       // 图片尺寸、颜色空间和色度采样信息
} jpeg_session_t;

/* HAL JPEG 回调没有用户上下文参数，因此当前实现使用单个全局会话。 */
static jpeg_session_t g_jpeg;
static const uint8_t *g_jpeg_input = NULL;
static volatile uint8_t s_encoding;
static uint8_t *s_encode_output;
static uint32_t s_encode_capacity;

static int jpeg_acquire(void)
{
  int acquired;
  taskENTER_CRITICAL();
  acquired = !g_jpeg.active;
  if (acquired) g_jpeg.active = 1U;
  taskEXIT_CRITICAL();
  return acquired;
}

/* 文件内部辅助函数，不作为硬件 JPEG 模块的公共接口。 */
static void jpeg_cache_clean(const void *address, uint32_t length);
static void jpeg_cache_invalidate(void *address, uint32_t length);
static int jpeg_convert_to_rgb565(void);
static int jpeg_copy_rgb565_to_lcd(uint16_t x, uint16_t y);

/**
 * @brief  使用 JPEG Codec 解码 JPEG，并将图像输出到指定 LCD 坐标
 * @param  data JPEG 压缩数据指针
 * @param  size JPEG 压缩数据长度（字节数）
 * @param  x    图片左上角 X 坐标（0 为 LCD 左边界）
 * @param  y    图片左上角 Y 坐标（0 为 LCD 上边界）
 * @return 0 表示解码并显示成功，-1 表示参数、硬件解码、转换或显示失败
 *
 * 执行步骤：
 * 1. 检查参数、JPEG SOI 标记和会话状态；
 * 2. 将码流复制到 SDRAM，并按 32 字节 MDMA 传输单元补齐；
 * 3. 启动 JPEG Codec 和 MDMA，等待完成、错误或超时；
 * 4. 失效 MCU 缓冲 Cache，将 YCbCr MCU 数据转换为 RGB565；
 * 5. 按 LCD 行跨度将 RGB565 图像复制到 LTDC 帧缓冲。
 *
 * @note 当前函数同步等待异步硬件回调，且全局会话只允许一次解码操作。
 */
int jpeg_hw_decode(const uint8_t *data, uint32_t size, uint16_t x, uint16_t y)
{
  uint32_t input_size;
  uint32_t first_input;
  uint32_t start_tick;

  if ((data == NULL) || (size < 4U))
  {
    return -1;
  }

  /* JPEG 文件必须以 SOI 标记 0xFFD8 开始。 */
  if ((data[0] != 0xFFU) || (data[1] != 0xD8U))
  {
    return -1;
  }

  /* 当前 HAL 回调共享全局状态，不允许两个调用者同时解码。 */
  /*
   * HAL 会把输入长度向下截断到 MDMA BufferTransferLength 的整数倍。
   * 先向上补齐可避免非对齐 JPEG 的末尾 EOI 标记被截掉。
   */
  input_size = (size + JPEG_MDMA_TRANSFER_UNIT - 1U) &
               ~(JPEG_MDMA_TRANSFER_UNIT - 1U);
  if (input_size > JPEG_INPUT_CHUNK_SIZE)
  {
    return -1;
  }

  if (!jpeg_acquire()) return -1;
  taskENTER_CRITICAL();
  memset(&g_jpeg, 0, sizeof(g_jpeg));
  g_jpeg.active = 1U;
  taskEXIT_CRITICAL();

  /*
   * 输入源可能位于 Flash 或任意 RAM。统一复制到 MDMA 可访问的 SDRAM，
   * 补零不会影响已经由 EOI 标记结束的 JPEG 码流。
   */
  memcpy(JPEG_INPUT_BUFFER, data, size);
  memset(JPEG_INPUT_BUFFER + size, 0, input_size - size);
  jpeg_cache_clean(JPEG_INPUT_BUFFER, input_size);

  g_jpeg_input = JPEG_INPUT_BUFFER;
  g_jpeg.in_size = input_size;
  g_jpeg.active = 1U;

  first_input = input_size;
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

  /* HAL_JPEG_Decode_DMA() 非阻塞，后续状态由 JPEG/MDMA 中断回调更新。 */
  if (HAL_JPEG_Decode_DMA(&hjpeg,
                          JPEG_INPUT_BUFFER,
                          first_input,
                          JPEG_MCU_BUFFER,
                          JPEG_OUTPUT_BUFFER_SIZE) != HAL_OK)
  {
    g_jpeg.active = 0U;
    return -1;
  }

  /* 等待中断更新会话状态；WFI 可降低等待期间的 CPU 活动。 */
  start_tick = HAL_GetTick();

  while (!g_jpeg.done &&
         !g_jpeg.error &&
         ((HAL_GetTick() - start_tick) < 5000U))
  {
    __WFI();
  }

  /* 超时不一定设置 HAL 错误码，因此也以 done 标志判断是否真正完成。 */
  if (!g_jpeg.done || g_jpeg.error)
  {
    HAL_JPEG_Abort(&hjpeg);
    g_jpeg.active = 0U;

    LOG_ERROR("JPEG", "HW JPEG failed, error=0x%08lx",
              HAL_JPEG_GetError(&hjpeg));
    return -1;
  }

  /* MDMA 已写完 MCU 缓冲，CPU 读取前丢弃对应的旧 Cache Line。 */
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

  LOG_INFO("JPEG", "HW JPEG %lux%lu, MCU=%lu bytes",
           g_jpeg.info.ImageWidth,
           g_jpeg.info.ImageHeight,
           g_jpeg.out_size);

  g_jpeg.active = 0U;
  return 0;
}

/**
 * @brief  保存 JPEG Codec 解析得到的图片信息
 * @param  hjpeg JPEG 外设句柄
 * @param  info  JPEG 头中的尺寸、颜色空间和色度采样信息
 *
 * @note 该回调只表示 JPEG 头解析完成，不表示整张图片已经解码完成。
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
 * @brief  在当前输入块被消耗后向 JPEG Codec 提供下一块码流
 * @param  hjpeg JPEG 外设句柄
 * @param  length 当前输入块已消耗的字节数
 *
 * @note 当全部输入已提交时，必须使用长度 0 配置输入缓冲，通知 HAL
 *       不再有后续数据。当前输入已整体复制到连续 SDRAM 缓冲中。
 */
void HAL_JPEG_GetDataCallback(JPEG_HandleTypeDef *hjpeg, uint32_t length)
{
  uint32_t remaining;
  uint32_t next_length;

  if (!g_jpeg.active)
  {
    return;
  }

  /* 防止异常回调长度导致无符号减法下溢和输入地址越界。 */
  if (length > g_jpeg.in_size - g_jpeg.in_off)
  {
    g_jpeg.error = 1U;
    HAL_JPEG_Pause(hjpeg, JPEG_PAUSE_RESUME_INPUT);
    return;
  }

  g_jpeg.in_off += length;
  remaining = g_jpeg.in_size - g_jpeg.in_off;

  /* 长度 0 是 HAL JPEG 表示“输入流已经结束”的约定。 */
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
 * @brief  记录 JPEG Codec 已写入输出缓冲的 MCU 数据长度
 * @param  hjpeg JPEG 外设句柄
 * @param  data  本次输出数据的首地址
 * @param  length 本次输出数据长度（字节数）
 *
 * @note 当前实现要求整张图片的 MCU 数据不超过 32 KiB，因此这里只累计
 *       输出长度，不配置下一输出缓冲。大图需要在此处接入分块或双缓冲。
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

  if (s_encoding)
  {
    if (length > s_encode_capacity - g_jpeg.out_size)
    {
      g_jpeg.error = 1U;
      HAL_JPEG_Pause(hjpeg, JPEG_PAUSE_RESUME_OUTPUT);
      return;
    }
    g_jpeg.out_size += length;
    uint32_t remaining = s_encode_capacity - g_jpeg.out_size;
    if (remaining < JPEG_OUTPUT_BUFFER_SIZE)
    {
      /* 缓冲已满时暂停，任务负责终止，绝不覆盖已编码的数据。 */
      HAL_JPEG_Pause(hjpeg, JPEG_PAUSE_RESUME_OUTPUT);
      if (length == JPEG_OUTPUT_BUFFER_SIZE) g_jpeg.error = 1U;
    }
    else
    {
      HAL_JPEG_ConfigOutputBuffer(hjpeg, s_encode_output + g_jpeg.out_size,
                                 JPEG_OUTPUT_BUFFER_SIZE);
    }
    return;
  }

  /* 检查累计长度，避免后续颜色转换读取固定 MCU 缓冲之外的数据。 */
  if (length > JPEG_OUTPUT_BUFFER_SIZE - g_jpeg.out_size)
  {
    g_jpeg.error = 1U;
    return;
  }

  g_jpeg.out_size += length;
}

/**
 * @brief  标记 JPEG 硬件解码完成
 * @param  hjpeg JPEG 外设句柄
 *
 * @note HAL 已传输完最后一段输出数据后才调用该回调。
 */
void HAL_JPEG_DecodeCpltCallback(JPEG_HandleTypeDef *hjpeg)
{
  (void)hjpeg;

  if (g_jpeg.active)
  {
    g_jpeg.done = 1U;
  }
}

void HAL_JPEG_EncodeCpltCallback(JPEG_HandleTypeDef *hjpeg)
{
  (void)hjpeg;
  if (g_jpeg.active && s_encoding) g_jpeg.done = 1U;
}

/** RGB565 整帧转 MCU 后使用 JPEG/MDMA 编码，缓冲与 LCD/采集区隔离。 */
int jpeg_hw_encode(uint8_t *rgb565, uint8_t *output, uint32_t capacity, uint32_t *size)
{
  uint8_t *mcu = (uint8_t *)0xD0600000U;
  JPEG_ConfTypeDef info = {0};
  JPEG_RGBToYCbCr_Convert_Function convert;
  uint32_t mcus, converted, start, yield_tick;
  int result = -1;
  if (!rgb565 || !output || !size || capacity < JPEG_OUTPUT_BUFFER_SIZE ||
      (capacity % JPEG_OUTPUT_BUFFER_SIZE) != 0U || !jpeg_acquire()) return -1;
  g_jpeg.done = 0U;
  g_jpeg.error = 0U;
  g_jpeg.in_off = 0U;
  g_jpeg.out_size = 0U;
  s_encoding = 1U;
  s_encode_output = output;
  s_encode_capacity = capacity;
  info.ImageWidth = 800U;
  info.ImageHeight = 480U;
  info.ColorSpace = JPEG_YCBCR_COLORSPACE;
  info.ChromaSubsampling = JPEG_420_SUBSAMPLING;
  info.ImageQuality = 80U;
  JPEG_InitColorTables();
  if (JPEG_GetEncodeColorConvertFunc(&info, &convert, &mcus) != HAL_OK) goto finish;
  /* 每次转换一行 MCU，累计运行 4 ms 后让出 CPU，避免每行固定等待。
   * 高优先级相机和网络任务仍可抢占，输入始终是独立的完整快照。 */
  yield_tick = HAL_GetTick();
  for (uint32_t block = 0U; block < mcus; block += 50U)
  {
    if (convert(rgb565, mcu + block * 384U, block, 800U * 16U * 2U, &converted) != 50U ||
        converted != 50U * 384U) goto finish;
    if (HAL_GetTick() - yield_tick >= 4U)
    {
      osDelay(1U);
      yield_tick = HAL_GetTick();
    }
  }
  g_jpeg.in_size = mcus * 384U;
  g_jpeg_input = mcu;
  jpeg_cache_clean(mcu, g_jpeg.in_size);
  jpeg_cache_clean(output, capacity);
  jpeg_cache_invalidate(output, capacity);
  if (HAL_JPEG_ConfigEncoding(&hjpeg, &info) != HAL_OK) goto finish;
  if (HAL_JPEG_Encode_DMA(&hjpeg, mcu, JPEG_INPUT_CHUNK_SIZE,
                          output, JPEG_OUTPUT_BUFFER_SIZE) != HAL_OK) goto finish;
  start = HAL_GetTick();
  while (!g_jpeg.done && !g_jpeg.error && HAL_GetTick() - start < 2000U) osDelay(1U);
  if (!g_jpeg.done || g_jpeg.error) goto finish;
  /* HAL 在 DMA 结束后用 CPU 读取 FIFO 尾部；先写回这些脏行再失效，
   * 避免丢掉 CPU 写入的 EOI。DMA 长度按 32 字节对齐，尾部另起缓存行。 */
  jpeg_cache_clean(output, g_jpeg.out_size);
  jpeg_cache_invalidate(output, g_jpeg.out_size);
  if (g_jpeg.out_size < 4U || output[0] != 0xFFU || output[1] != 0xD8U) goto finish;
  /* FIFO 按 32 位读取，EOI 后至多包含三个填充字节，不发送填充。 */
  uint32_t encoded_size = g_jpeg.out_size;
  while (encoded_size >= 4U && g_jpeg.out_size - encoded_size <= 3U)
  {
    if (output[encoded_size - 2U] == 0xFFU && output[encoded_size - 1U] == 0xD9U) break;
    encoded_size--;
  }
  if (encoded_size < 4U || g_jpeg.out_size - encoded_size > 3U) goto finish;
  g_jpeg.out_size = encoded_size;
  *size = g_jpeg.out_size;
  result = 0;
finish:
  if (result != 0) HAL_JPEG_Abort(&hjpeg);
  s_encoding = 0U;
  __DMB();
  g_jpeg.active = 0U;
  return result;
}

/**
 * @brief  标记 JPEG Codec 或 MDMA 解码错误
 * @param  hjpeg JPEG 外设句柄
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
 * @brief  将指定范围的 D-Cache 脏数据写回主存
 * @param  address 缓存起始地址
 * @param  length  缓存长度（字节数）
 *
 * @note Cortex-M7 D-Cache Line 为 32 字节，因此操作范围向两端扩展到
 *       Cache Line 边界。用于 CPU 写、MDMA 或 LTDC 读的所有权切换。
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
 * @brief  失效指定范围的 D-Cache，使 CPU 后续从主存重新读取数据
 * @param  address 缓存起始地址
 * @param  length  缓存长度（字节数）
 *
 * @note 用于 MDMA 写、CPU 读的所有权切换；操作范围按 32 字节 Cache
 *       Line 对齐，避免 CPU 继续读取 MDMA 写入前的缓存副本。
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
 * @brief  根据 JPEG 头信息将 YCbCr MCU 数据转换为 RGB565
 * @return 0 表示转换成功，-1 表示图片尺寸、颜色格式或 MCU 数量异常
 *
 * @note JPEG_GetDecodeColorConvertFunc() 根据颜色空间和色度采样方式选择
 *       对应转换函数。RGB 输出按图片自身宽度紧密排列，不含 LCD 行偏移。
 */
static int jpeg_convert_to_rgb565(void)
{
  JPEG_ConfTypeDef convert_info;
  JPEG_YCbCrToRGB_Convert_Function convert;
  uint32_t mcu_width;
  uint32_t mcu_height;
  uint32_t image_mcu_count;
  uint32_t converted_bytes;
  uint32_t converted_mcus;

  if (!g_jpeg.info_ready)
  {
    return -1;
  }

  /* 当前 RGB 缓冲和 LCD 均按最大 800 x 480 RGB565 设计。 */
  if ((g_jpeg.info.ImageWidth > 800U) ||
      (g_jpeg.info.ImageHeight > 480U))
  {
    return -1;
  }

  if ((g_jpeg.info.ColorSpace == JPEG_YCBCR_COLORSPACE) &&
      (g_jpeg.info.ChromaSubsampling == JPEG_420_SUBSAMPLING))
  {
    mcu_width = 16U;
    mcu_height = 16U;
  }
  else if ((g_jpeg.info.ColorSpace == JPEG_YCBCR_COLORSPACE) &&
           (g_jpeg.info.ChromaSubsampling == JPEG_422_SUBSAMPLING))
  {
    mcu_width = 16U;
    mcu_height = 8U;
  }
  else
  {
    mcu_width = 8U;
    mcu_height = 8U;
  }

  /*
   * jpeg_utils 按完整 MCU 写入边缘像素，却使用 ImageWidth 作为 RGB 行跨度。
   * 原图宽度不是 MCU 整数倍时，右侧填充像素会串写到下一行。转换阶段
   * 临时使用 MCU 对齐尺寸，为边缘填充像素保留独立空间；显示时再裁剪。
   */
  convert_info = g_jpeg.info;
  convert_info.ImageWidth = (g_jpeg.info.ImageWidth + mcu_width - 1U) &
                            ~(mcu_width - 1U);
  convert_info.ImageHeight = (g_jpeg.info.ImageHeight + mcu_height - 1U) &
                             ~(mcu_height - 1U);

  if ((convert_info.ImageWidth * convert_info.ImageHeight * 2U) >
      JPEG_RGB_BUFFER_SIZE)
  {
    return -1;
  }

  if (JPEG_GetDecodeColorConvertFunc(&convert_info,
                                     &convert,
                                     &image_mcu_count) != HAL_OK)
  {
    return -1;
  }

  g_jpeg.rgb_stride = convert_info.ImageWidth;
  converted_bytes = 0U;

  /* 转换函数只处理完整 MCU，返回值应与 JPEG 头计算的 MCU 总数一致。 */
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
 * @brief  将紧密排列的 RGB565 图像复制到指定 LCD 显示区域
 * @param  x LCD 显示区域的起始 X 坐标
 * @param  y LCD 显示区域的起始 Y 坐标
 * @return 0 表示复制成功，-1 表示目标区域超出 LCD 边界
 *
 * @note RGB 缓冲每行只有 ImageWidth 个像素，而 LTDC 帧缓冲每行有
 *       lcddev.width 个像素，因此必须逐行复制并使用 LCD 宽度计算目标地址。
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
             row * g_jpeg.rgb_stride;

    destination = (uint16_t *)LTDC_FRAME_BUF_ADDR +
                  (y + row) * lcddev.width +
                  x;

    memcpy(destination,
           source,
           width * sizeof(uint16_t));

    /* CPU 写入可缓存 SDRAM 后执行 Clean，确保 LTDC 能读到最新像素。 */
    jpeg_cache_clean(destination,
                     width * sizeof(uint16_t));
  }

  __DSB();
  return 0;
}
