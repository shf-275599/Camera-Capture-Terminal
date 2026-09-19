#ifndef APP_HW_JPEG_H
#define APP_HW_JPEG_H

#include <stdio.h>
#include <stdint.h>

int jpeg_hw_decode(uint8_t *in, uint32_t in_size, uint8_t *mcu_out);

#endif /* APP_HW_JPEG_H */
