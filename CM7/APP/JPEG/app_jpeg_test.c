#include "app_jpeg_test.h"

#include <stdio.h>
#include <string.h>

#include "jpeg_test_data.h"
#include "lcd.h"
#include "tjpgd_utils.h"

#define JPEG_WORK_POOL_SIZE  (8192U)

typedef struct
{
    const uint8_t *data;
    size_t size;
    size_t position;
    uint16_t x;
    uint16_t y;
} jpeg_test_context_t;

static uint8_t jpeg_work_pool[JPEG_WORK_POOL_SIZE];

static size_t jpeg_test_input(JDEC *decoder, uint8_t *buffer, size_t count)
{
    jpeg_test_context_t *context = (jpeg_test_context_t *)decoder->device;
    size_t remaining = context->size - context->position;

    if (count > remaining)
    {
        count = remaining;
    }

    if (buffer != NULL)
    {
        memcpy(buffer, context->data + context->position, count);
    }

    context->position += count;
    return count;
}

static int jpeg_test_output(JDEC *decoder, void *bitmap, JRECT *rect)
{
    jpeg_test_context_t *context = (jpeg_test_context_t *)decoder->device;
    const uint16_t *source = (const uint16_t *)bitmap;
    uint16_t width = rect->right - rect->left + 1U;
    uint16_t height = rect->bottom - rect->top + 1U;
    uint16_t row;

    if ((context->x + rect->right >= lcddev.width) ||
        (context->y + rect->bottom >= lcddev.height))
    {
        return 0;
    }

    for (row = 0U; row < height; row++)
    {
        uint16_t *destination = (uint16_t *)LTDC_FRAME_BUF_ADDR +
                                ((context->y + rect->top + row) * lcddev.width) +
                                context->x + rect->left;
        uintptr_t cache_start;
        uintptr_t cache_end;

        memcpy(destination, source + (row * width), width * sizeof(uint16_t));

        cache_start = (uintptr_t)destination & ~(uintptr_t)31U;
        cache_end = ((uintptr_t)destination + width * sizeof(uint16_t) + 31U) &
                    ~(uintptr_t)31U;
        SCB_CleanDCache_by_Addr((uint32_t *)cache_start,
                                (int32_t)(cache_end - cache_start));
    }

    return 1;
}

void app_jpeg_test_run(void)
{
    JDEC decoder;
    JRESULT result;
    jpeg_test_context_t context = {
        .data = jpeg_test_data,
        .size = jpeg_test_data_size,
        .position = 0U,
        .x = 0U,
        .y = 0U,
    };

    result = jd_prepare(&decoder, jpeg_test_input, jpeg_work_pool,
                        sizeof(jpeg_work_pool), &context);
    if (result != JDR_OK)
    {
        printf("CM7: JPEG prepare failed, result=%d\n", (int)result);
        return;
    }

    printf("CM7: JPEG image %ux%u, data=%u bytes\n",
           decoder.width, decoder.height, (unsigned int)context.size);

    if ((decoder.width > lcddev.width) || (decoder.height > lcddev.height))
    {
        printf("CM7: JPEG image is larger than LCD\n");
        return;
    }

    context.x = (lcddev.width - decoder.width) / 2U;
    context.y = (lcddev.height - decoder.height) / 2U;
    result = jd_decomp(&decoder, jpeg_test_output, 0U);
    __DSB();

    printf("CM7: JPEG decode result=%d\n", (int)result);
}
