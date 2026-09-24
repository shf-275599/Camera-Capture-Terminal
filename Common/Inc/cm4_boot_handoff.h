#ifndef __CM4_BOOT_HANDOFF_H__
#define __CM4_BOOT_HANDOFF_H__

#include <stdint.h>

/** 小端 'CM4' */
#define CM4_BOOT_HANDOFF_MAGIC      (0x4A344D43UL)

/** OpenAMP SHM 止于 0x3800F000；状态字之后空闲 */
#define CM4_BOOT_HANDOFF_MAGIC_ADDR (0x3800F010UL)
#define CM4_BOOT_HANDOFF_BASE_ADDR  (0x3800F014UL)

uint8_t cm4_boot_handoff_jump_from_d3(void);

#endif

