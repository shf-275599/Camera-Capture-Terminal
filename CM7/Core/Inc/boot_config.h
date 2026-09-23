#ifndef __BOOT_CONFIG_H__
#define __BOOT_CONFIG_H__

/* CM7 Flash 分区：Bootloader 128KB，APP 从 0x08020000 开始 */
#define BOOTLOADER_FLASH_SIZE (128U * 1024U)
#define CM7_APP_FLASH_BASE (0x08000000UL + BOOTLOADER_FLASH_SIZE)

/* Boot 跳入 APP 时在 D1 RAM 尾留魔数，APP SystemInit 据此跳过 RCC 全复位 */
#define BOOT_JUNP_FLAG_ADDR (0x2001FFF0UL)
#define BOOT_JUNP_MAGIC (0xB00710ADUL)

#endif
