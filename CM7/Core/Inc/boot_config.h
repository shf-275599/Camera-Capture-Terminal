#ifndef __BOOT_CONFIG_H__
#define __BOOT_CONFIG_H__

/* Boot 跳入 APP 时在 D1 RAM 尾留魔数，APP SystemInit 据此跳过 RCC 全复位 */
#define BOOT_JUNP_FLAG_ADDR (0x2001FFF0UL)
#define BOOT_JUNP_MAGIC (0xB00710ADUL)

#endif
