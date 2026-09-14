#include "app_hw_jpeg.h"
#include <string.h>
#include <stdio.h>
#include "jpeg.h"

#define JPEG_CHUNK_SIZE    (64U * 1024U)

uint8_t *jpeg_in = NULL;    /* 码流缓冲 */
uint8_t *jpeg_mcu = NULL;   /* MCU 缓冲 */
uint32_t jpeg_in_size = 0;     /* 码流总长度 */

typedef struct {
  volatile uint8_t  active;
  volatile uint8_t  done;
  volatile uint8_t  error;
  volatile uint32_t in_off;
  volatile uint32_t in_size;
  volatile uint32_t out_size;
  JPEG_ConfTypeDef  info;
} jpeg_session_t;

static jpeg_session_t g_jpeg;

int jpeg_hw_decode(uint8_t *in, uint32_t in_size, uint8_t *mcu_out)
{
  uint32_t first_in;

  memset(&g_jpeg, 0, sizeof(g_jpeg));
  g_jpeg.in_size = in_size;
  g_jpeg.active  = 1;

  first_in = (in_size > JPEG_CHUNK_SIZE) ? JPEG_CHUNK_SIZE : in_size;

  /* 只提交首块，后续由回调滑窗推进 */
  if (HAL_JPEG_Decode_DMA(&hjpeg, in, first_in,
                          mcu_out, JPEG_CHUNK_SIZE) != HAL_OK) {
    g_jpeg.active = 0;
    return -1;
  }

  /* 等完成 / 出错 / 超时 */
  uint32_t t0 = HAL_GetTick();
  while (!g_jpeg.done && !g_jpeg.error &&
         (HAL_GetTick() - t0) < 5000U) {
    __WFI();
  }

  g_jpeg.active = 0;
  if (!g_jpeg.done || g_jpeg.error) {
    HAL_JPEG_Abort(&hjpeg);
    return -1;
  }
  return 0;   /* MCU 已在 mcu_out，长度为 g_jpeg.out_size */
}