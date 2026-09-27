/**
 * @file    boot_slot.h
 * @brief   CM7 / CM4 Flash 双槽布局（镜像元数据在 EEPROM）
 *
 * 一句话总结：1 Bank 给 1 核，核内再按 sector 边界切成 Boot/Stub + 双槽 + 隔离区，换来安全、可回滚、双核独立的 OTA。
 *
 * 共 2MB，分成两个 1MB Bank：Bank1 @ 0x08000000、Bank2 @ 0x08100000
 * 擦除最小单位是 sector，每 sector = 128KB，每个 Bank 共 8 个 sector（S0~S7）
 *
 * Bank1 (1MB) — CM7：
 *   Boot        0x08000000  128KB   S0
 *   Slot A      0x08020000  384KB   至 0x0807FFFF（S1~S3）
 *   Reserve     0x08080000  128KB   至 0x0809FFFF（S4 预留，不参与 OTA）
 *   Slot B      0x080A0000  384KB   至 0x080FFFFF（S5~S7）
 *
 * Boot 放 S0：芯片复位后 M7 默认从 0x08000000 读向量表。
 * Slot A/B = 两侧各 3 个 sector：分区严格按 sector 边界对齐——这是关键要求，
 *            因为擦除 Granularity 是 128KB，如果 Slot 边界插在 sector 中间，
 *            擦其中一个就会擦到邻居。
 * Reserve = 1 个整 sector（S4）：A 槽尾（S3）与 B 槽头（S5）之间留一道"防火墙"。
 *
 * Bank2 (1MB) — CM4：
 *   Stub        0x08100000  128KB   S0（CM4 的固定引导入口，OTA 永不擦）
 *   Slot A      0x08120000  384KB   至 0x0817FFFF（S1~S3）
 *   Reserve     0x08180000  128KB   至 0x0819FFFF（S4 预留）
 *   Slot B      0x081A0000  384KB   至 0x081FFFFF（S5~S7）
 *
 * 与 Bank1 完全对称，只是 S0 从"完整 Bootloader"变成 Stub：
 * 固定入口、只干一件事——从 D3 SRAM 读 cm4_boot_handoff 的手写 magic + base
 * 再跳去 A/B（见 Common/Inc/cm4_boot_handoff.h）。
 * 它永远不参与 OTA，所以无论 A/B 怎么擦写，CM4 总有落点。
 */
#ifndef __BOOT_SLOT_H__
#define __BOOT_SLOT_H__

#include <stdint.h>

/**
 * @brief   Bank1 地址范围
 */
/* Bootloader Flash 地址范围 */
#define CM7_BOOT_FLASH_BASE (0x08000000UL)
#define CM7_BOOT_FLASH_SIZE (128U * 1024U)
/* CM7 APP A、B 扇区 Flash 地址范围 */
#define CM7_APP_SLOT_A_BASE (0x08020000UL)
#define CM7_APP_SLOT_B_BASE (0x080A0000UL)
/* CM7 APP 扇区数量 */
#define CM7_APP_SLOT_COUNT (2U)
/* CM7 APP A、B 扇区大小 */
#define CM7_APP_SLOT_A_SIZE (384U * 1024U)
#define CM7_APP_SLOT_B_SIZE (384U * 1024U)
/* CM7 APP A、B 扇区索引 */
#define CM7_APP_SLOT_A_INDEX (0U)
#define CM7_APP_SLOT_B_INDEX (1U)

/**
 * @brief   Bank2 地址范围
 */
/* CM4 固定引导入口 */
#define CM4_BOOT_STUB_BASE (0x08100000UL)
#define CM4_BOOT_STUB_SIZE (128U * 1024U)
/* CM4 APP A、B 扇区 Flash 地址范围 */
#define CM4_APP_SLOT_A_BASE (0x08120000UL)
#define CM4_APP_SLOT_B_BASE (0x081A0000UL)
/* CM4 APP 扇区数量 */
#define CM4_APP_SLOT_COUNT (2U)
/* CM4 APP A、B 扇区大小 */
#define CM4_APP_SLOT_A_SIZE (384U * 1024U)
#define CM4_APP_SLOT_B_SIZE (384U * 1024U)
/* CM4 APP A、B 扇区索引 */
#define CM4_APP_SLOT_A_INDEX (2U)
#define CM4_APP_SLOT_B_INDEX (3U)

int boot_slot_is_valid(uint8_t slot_index);
uint8_t boot_slot_index_is_valid(uint8_t slot_index);
uint32_t boot_slot_base_get(uint8_t slot_index);
uint32_t boot_slot_end_get(uint8_t slot_index);
uint32_t boot_slot_stack_region_is_valid(uint8_t slot_index, uint32_t stack);
uint8_t boot_get_slot_index_from_address(uint32_t address);

#endif
