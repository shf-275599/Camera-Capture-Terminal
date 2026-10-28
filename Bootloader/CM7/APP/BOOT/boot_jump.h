#ifndef __BOOT_JUMP_H__
#define __BOOT_JUMP_H__

#include <stdint.h>
#include <stdbool.h>

bool boot_app_is_valid(uint32_t app_base);
void boot_jump_to_app(uint32_t app_base);

#endif
