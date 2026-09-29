#ifndef __BOOT_CONFIG_H__
#define __BOOT_CONFIG_H__

/* Bootloader Firmware 固件标签 */
#define BOOT_FW_TAG "Bootloader-CameraDoubleCPU-1.0.0-20260801"

/* Boot 跳入 APP 时在 D1 RAM 尾留魔数，APP SystemInit 据此跳过 RCC 全复位 */
#define BOOT_JUMP_FLAG_ADDR (0x2001FFF0UL)
#define BOOT_JUMP_MAGIC (0xB00710ADUL)

#endif
