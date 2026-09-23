#ifndef APP_HW_JPEG_H
#define APP_HW_JPEG_H

#include <stdio.h>
#include <stdint.h>

int jpeg_hw_decode(const uint8_t *data, uint32_t size, uint16_t x, uint16_t y);

#endif /* APP_HW_JPEG_H */
