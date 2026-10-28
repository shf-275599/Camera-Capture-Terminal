#ifndef APP_HW_JPEG_H
#define APP_HW_JPEG_H

#include <stdio.h>
#include <stdint.h>

int jpeg_hw_decode(const uint8_t *data, uint32_t size, uint16_t x, uint16_t y);
/* 输出缓冲由调用者保持有效，编码与解码共享一个硬件会话。 */
int jpeg_hw_encode(uint8_t *rgb565, uint8_t *output, uint32_t capacity, uint32_t *size);

#endif /* APP_HW_JPEG_H */
