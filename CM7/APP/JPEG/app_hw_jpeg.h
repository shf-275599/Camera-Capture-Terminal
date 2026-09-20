#ifndef APP_HW_JPEG_H
#define APP_HW_JPEG_H

#include <stdio.h>
#include <stdint.h>

int jpeg_hw_decode(const uint8_t *data, uint32_t size, uint16_t x, uint16_t y);
static void jpeg_cache_clean(const void *address, uint32_t length);
static void jpeg_cache_invalidate(void *address, uint32_t length);
static int jpeg_convert_to_rgb565(void);
static int jpeg_copy_rgb565_to_lcd(uint16_t x, uint16_t y);

#endif /* APP_HW_JPEG_H */
