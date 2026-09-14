/**
 * @file    app_jpeg_test.c
 * @brief   使用 TJpgDec 解码内置 JPEG 数据并显示到 LTDC LCD
 *
 * 数据流如下：
 * jpeg_test_data[]（Flash） -> TJpgDec -> RGB565 MCU 像素块
 * -> LTDC 帧缓冲（SDRAM） -> LCD。
 *
 * TJpgDec 不直接访问具体存储设备和显示设备，而是通过输入、输出回调
 * 完成数据读取和像素输出。本文件的输入源是编译进固件的 const 数组，
 * 后续接入 FatFS 时可将输入回调替换为 f_read()，解码及显示流程无需改变。
 */
#include "app_jpeg.h"

#include <stdio.h>
#include <string.h>

#include "lcd.h"
#include "tjpgd_utils.h"

/* TJpgDec 在 jd_prepare() 中从该内存池分配输入缓冲、Huffman 表和工作缓冲。 */
#define JPEG_WORK_POOL_SIZE  (8192U)

/**
 * @brief JPEG 解码过程的输入状态和显示位置
 *
 * 该结构通过 jd_prepare() 的 dev 参数保存到 JDEC::device，输入和输出
 * 回调因此可以共享同一个会话上下文，而不需要使用全局读写状态。
 */
typedef struct
{
    const uint8_t *data; /* JPEG 压缩数据首地址 */
    size_t size;         /* JPEG 压缩数据总字节数 */
    size_t position;     /* 输入回调当前读取偏移 */
    uint16_t x;          /* 图片左上角在 LCD 中的 X 坐标 */
    uint16_t y;          /* 图片左上角在 LCD 中的 Y 坐标 */
} jpeg_context_t;

/* 静态分配可避免依赖 FreeRTOS 堆；解码期间该内存池必须保持有效。 */
static uint8_t jpeg_work_pool[JPEG_WORK_POOL_SIZE];

/**
 * @brief  向 TJpgDec 提供 JPEG 字节流
 * @param  decoder TJpgDec 解码器对象，device 指向 jpeg_context_t
 * @param  buffer  目标缓冲区；为 NULL 时仅跳过 count 字节，不复制数据
 * @param  count   TJpgDec 请求读取或跳过的字节数
 * @return 实际读取或跳过的字节数；到达数据末尾时可能小于 count
 *
 * @note TJpgDec 在解析 APP、COM 等不需要处理的 JPEG 段时，会将 buffer
 *       传为 NULL。即使不执行 memcpy，也必须推进 position，否则解析器
 *       会重复读取同一段数据。
 */
static size_t jpeg_input(JDEC *decoder, uint8_t *buffer, size_t count)
{
    /* jd_prepare() 已将调用者传入的会话上下文保存到 decoder->device。 */
    jpeg_context_t *context = (jpeg_context_t *)decoder->device;
    size_t remaining = context->size - context->position;

    /* 将本次读取限制在数组边界内，避免读取 const 数组之外的数据。 */
    if (count > remaining)
    {
        count = remaining;
    }

    /* buffer == NULL 是 TJpgDec 的“跳过数据”约定。 */
    if (buffer != NULL)
    {
        memcpy(buffer, context->data + context->position, count);
    }

    /* 读取和跳过操作都要更新流位置，并返回实际处理的字节数。 */
    context->position += count;
    return count;
}

/**
 * @brief  将 TJpgDec 输出的 RGB565 像素块写入 LTDC 帧缓冲
 * @param  decoder TJpgDec 解码器对象，device 指向 jpeg_context_t
 * @param  bitmap  当前 MCU 解码后的 RGB565 像素数组，按行紧密排列
 * @param  rect    当前输出块在 JPEG 图片中的坐标，四个边界均为闭区间
 * @return 1 表示成功并继续解码，0 表示终止解码
 *
 * @note TJpgDec 按 MCU（Minimum Coded Unit）分块调用该函数，而不是一次
 *       输出整张图片。边缘 MCU 可能比内部 MCU 小，因此每次都必须根据
 *       rect 重新计算宽度和高度。
 */
static int jpeg_output(JDEC *decoder, void *bitmap, JRECT *rect)
{
    jpeg_context_t *context = (jpeg_context_t *)decoder->device;
    const uint16_t *source = (const uint16_t *)bitmap;
    /* JRECT 的 right 和 bottom 属于有效区域，因此尺寸计算需要加 1。 */
    uint16_t width = rect->right - rect->left + 1U;
    uint16_t height = rect->bottom - rect->top + 1U;
    uint16_t row;

    /* 将图片偏移叠加到 MCU 坐标后检查屏幕边界，防止破坏帧缓冲。 */
    if ((context->x + rect->right >= lcddev.width) ||
        (context->y + rect->bottom >= lcddev.height))
    {
        return 0;
    }

    for (row = 0U; row < height; row++)
    {
        /*
         * LTDC 帧缓冲采用 RGB565，每像素 16 位，按 lcddev.width 个像素
         * 作为一行。源像素块没有行间空隙，而目标帧缓冲通常比块更宽，
         * 因此逐行复制，避免覆盖图片区域右侧的其他像素。
         */
        uint16_t *destination = (uint16_t *)LTDC_FRAME_BUF_ADDR +
                                ((context->y + rect->top + row) * lcddev.width) +
                                context->x + rect->left;
        uintptr_t cache_start;
        uintptr_t cache_end;

        memcpy(destination, source + (row * width), width * sizeof(uint16_t));

        /*
         * SDRAM 在 MPU 中被配置为可缓存，CPU 的 memcpy 可能只更新 D-Cache，
         * 而 LTDC 直接从 SDRAM 取数。Cortex-M7 的 D-Cache 行为 32 字节，
         * 所以清理地址和长度都向 Cache Line 边界扩展，确保 LTDC 能看到
         * 本行刚写入的全部像素。
         */
        cache_start = (uintptr_t)destination & ~(uintptr_t)31U;
        cache_end = ((uintptr_t)destination + width * sizeof(uint16_t) + 31U) &
                    ~(uintptr_t)31U;
        SCB_CleanDCache_by_Addr((uint32_t *)cache_start,
                                (int32_t)(cache_end - cache_start));
    }

    return 1;
}

/**
 * @brief  解码内置测试 JPEG，并将其居中显示在 LCD 上
 * @param  jpeg_data  JPEG 图片数据指针
 * @param  jpeg_data_size JPEG 图片数据大小（字节数）
 * @return None
 *
 * 执行步骤：
 * 1. 初始化内存输入上下文；
 * 2. 调用 jd_prepare() 解析 JPEG 头并建立解码表；
 * 3. 检查原图尺寸是否超出 LCD；
 * 4. 计算居中坐标并调用 jd_decomp() 输出 RGB565 像素。
 *
 * @note 当前 scale 参数为 0，表示按原始尺寸解码。TJpgDec 支持 0~3，
 *       分别对应 1、1/2、1/4、1/8 尺寸。
 */
void app_jpeg_run(uint8_t *jpeg_data, unsigned int jpeg_data_size)
{
    /* JDEC 保存本次解码状态，只需在函数执行期间保持有效。 */
    JDEC decoder;
    JRESULT result;
    /* 输入数据位于 Flash；显示坐标在解析出图片尺寸后再计算。 */
    jpeg_context_t context = {
        .data = jpeg_data,
        .size = jpeg_data_size,
        .position = 0U,
        .x = 0U,
        .y = 0U,
    };

    /* 解析 JPEG 文件头，并从 jpeg_work_pool 中分配解码器内部数据。 */
    result = jd_prepare(&decoder, jpeg_input, jpeg_work_pool,
                        sizeof(jpeg_work_pool), &context);
    if (result != JDR_OK)
    {
        printf("CM7: JPEG prepare failed, result=%d\n", (int)result);
        return;
    }

    printf("CM7: JPEG image %ux%u, data=%u bytes\n",
           decoder.width, decoder.height, (unsigned int)context.size);

    /* 本测试不启用缩放，原图必须能够完整放入当前 LCD 逻辑分辨率。 */
    if ((decoder.width > lcddev.width) || (decoder.height > lcddev.height))
    {
        printf("CM7: JPEG image is larger than LCD\n");
        return;
    }

    /* 计算左上角坐标，使图片在 LCD 水平和垂直方向居中。 */
    context.x = (lcddev.width - decoder.width) / 2U;
    context.y = (lcddev.height - decoder.height) / 2U;

    /* scale=0 表示不缩放；解码器将重复调用 jpeg_output() 输出 MCU。 */
    result = jd_decomp(&decoder, jpeg_output, 0U);

    /* 确保所有显式内存操作在打印完成状态前已经结束。 */
    __DSB();

    printf("CM7: JPEG decode result=%d\n", (int)result);
}
