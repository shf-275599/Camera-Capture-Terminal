#ifndef APP_SW_JPEG_H
#define APP_SW_JPEG_H

#include <stdint.h>

void jpeg_sw_decode(uint8_t *jpeg_data, unsigned int jpeg_data_size,
                    uint16_t x, uint16_t y);

#endif /* APP_SW_JPEG_H */
