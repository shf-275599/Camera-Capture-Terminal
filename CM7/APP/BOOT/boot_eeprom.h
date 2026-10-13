/**
 * @file    boot_eeprom.h
 * @brief   EEPROM 双备份启动元数据（BL24C16F）
 *
 * 布局（2KB 芯片）：
 *   Copy0 @ 0x0000
 *   Copy1 @ 0x0100
 * 每次写入只改一份，seq 递增；启动时选 CRC 合法且 seq 更大的一份。
 *
 * struct_ver=2：在 CM7 槽信息之外增加 CM4 A/B 槽元数据。
 * 读到 ver=1 旧记录时自动升级为 ver=2（CM4 默认 Slot A）。
 */
#ifndef BOOT_EEPROM_H
#define BOOT_EEPROM_H

#include <stdint.h>
#include "boot_slot.h"

#define BOOT_EE_MAGIC (0x45454D41UL) /* "EEMA" */
#define BOOT_EE_STRUCT_VER (2U)
#define BOOT_EE_STRUCT_VER_V1 (1U)
#define BOOT_EE_FLAG_PENDING (1U << 0)
#define BOOT_EE_FLAG_CM4_PENDING (1U << 0)

#define BOOT_EE_COPY0_ADDR (0x0000U)
#define BOOT_EE_COPY1_ADDR (0x0100U)
#define BOOT_EE_COPY_SIZE (256U)

/**
 * @brief  EEPROM 双备份启动元数据结构体
 * @note   2 字节对齐，总大小 128 字节
 */
typedef struct __attribute__((packed))
{
    uint32_t magic;      // 魔数，用于识别是否为有效备份
    uint16_t struct_ver; // 结构版本，当前为 2
    uint16_t length;     // 数据长度，当前为 128 字节
    /* CM7 A/B */
    uint8_t active_slot;                      // 当前 CM7 槽索引
    uint8_t pending_slot;                     // 待启动 CM7 槽索引
    uint8_t boot_trial;                       // 试启动倒计时
    uint8_t flags;                            // 标志位，当前为 0
    uint32_t app_version[CM7_APP_SLOT_COUNT]; // CM7 应用版本号数组，每个槽 4 字节
    uint32_t app_size[CM7_APP_SLOT_COUNT];    // CM7 应用大小数组，每个槽 4 字节
    uint32_t app_crc[CM7_APP_SLOT_COUNT];     // CM7 应用 CRC32 校验和数组，每个槽 4 字节
    /* CM4 A/B */
    uint8_t cm4_active_slot;                      // CM4 当前 active 槽索引
    uint8_t cm4_pending_slot;                     // CM4 待启动 pending 槽索引
    uint8_t cm4_boot_trial;                       // CM4 试启动倒计时
    uint8_t cm4_flags;                            // CM4 标志位，当前为 0
    uint32_t cm4_app_version[CM4_APP_SLOT_COUNT]; // CM4 应用版本号数组，每个槽 4 字节
    uint32_t cm4_app_size[CM4_APP_SLOT_COUNT];    // CM4 应用大小数组，每个槽 4 字节
    uint32_t cm4_app_crc[CM4_APP_SLOT_COUNT];     // CM4 应用 CRC32 校验和数组，每个槽 4 字节
    uint32_t seq;                                 // 序列号，用于判断是否为最新备份
    uint32_t crc32;                               // 整体 CRC32 校验和，用于验证数据完整性
} boot_eeprom_meta_t;

/**
 * @brief  初始化 I2C EEPROM 驱动句柄
 * @retval 0 设备应答，-1 失败
 */
int boot_eeprom_init(void);

/**
 * @brief  从双备份区加载元数据（无效则填默认 active=A）
 * @retval 0 成功读到合法备份，1 使用默认值，-1 参数错误
 */
int boot_eeprom_load(boot_eeprom_meta_t *meta);

/**
 * @brief  写入下一份备份（双分区轮换）
 * @retval 0 成功，-1 失败
 */
int boot_eeprom_save(boot_eeprom_meta_t *meta);

/**
 * @brief  按 meta 选择应启动的 CM7 槽（pending 优先且须有效）
 */
uint8_t boot_eeprom_select_boot_slot(const boot_eeprom_meta_t *meta);

/**
 * @brief  CM7 试启动倒计时；用尽则取消 pending，回退 active
 * @retval 最终应启动的槽索引
 */
uint8_t boot_eeprom_prepare_boot(boot_eeprom_meta_t *meta);

/**
 * @brief  OTA 成功后由 APP 调用：提交 CM7 pending 为 active
 */
void boot_eeprom_commit_pending(boot_eeprom_meta_t *meta);

/**
 * @brief  标记 CM7 pending 槽（OTA 写完镜像后由 APP 调用）
 */
void boot_eeprom_set_pending(boot_eeprom_meta_t *meta, uint8_t slot_index,
                             uint32_t image_size, uint32_t image_crc32,
                             uint32_t version);

/**
 * @brief  按 meta 选择应启动的 CM4 槽（pending 优先且须有效）
 */
uint8_t boot_eeprom_select_cm4_boot_slot(const boot_eeprom_meta_t *meta);

/**
 * @brief  CM4 试启动倒计时；用尽则取消 pending，回退 active
 * @retval 最终应启动的 CM4 槽索引
 */
uint8_t boot_eeprom_prepare_cm4_boot(boot_eeprom_meta_t *meta);

/**
 * @brief  提交 CM4 pending 为 active
 */
void boot_eeprom_commit_cm4_pending(boot_eeprom_meta_t *meta);

/**
 * @brief  标记 CM4 pending 槽
 */
void boot_eeprom_set_cm4_pending(boot_eeprom_meta_t *meta, uint8_t slot_index,
                                 uint32_t image_size, uint32_t image_crc32,
                                 uint32_t version);

#endif /* BOOT_EEPROM_H */
