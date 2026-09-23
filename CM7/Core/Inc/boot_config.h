#ifndef __BOOT_CONFIG_H__
#define __BOOT_CONFIG_H__

/* CM7 Flash 分区表（H7 Flash 扇区擦除粒度 128KB，分区大小均为其整数倍）：
 * |    分区     |    地址      |   大小   |
 * | bootloader | 0x08000000   | 128KB    |
 * | app1       | 0x08020000   | 512KB    |
 * | app2       | 0x080A0000   | 512KB    |
 * | setting    | 0x08120000   | 128KB    |
 */
#define BOOT_SECTOR_ADDR      (0x08000000UL)
#define BOOT_SECTOR_SIZE      (128U * 1024U)
#define APP_SECTOR_ADDR       (0x08020000UL)
#define APP_SECTOR_SIZE       (512U * 1024U)
#define APP2_SECTOR_ADDR      (0x080A0000UL)
#define APP2_SECTOR_SIZE      (512U * 1024U)
#define SETTING_SECTOR_ADDR   (0x08120000UL)
#define SETTING_SECTOR_SIZE   (128U * 1024U)

#define BOOTLOADER_FLASH_SIZE BOOT_SECTOR_SIZE
#define CM7_APP_FLASH_BASE    APP_SECTOR_ADDR

/* Boot 跳入 APP 时在 D1 RAM 尾留魔数，APP SystemInit 据此跳过 RCC 全复位 */
#define BOOT_JUNP_FLAG_ADDR (0x2001FFF0UL)
#define BOOT_JUNP_MAGIC (0xB00710ADUL)

#endif
