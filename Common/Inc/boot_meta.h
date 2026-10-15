/**
 * @file    boot_meta.h
 * @brief   启动元数据（BKPSRAM 持久化）接口与数据结构
 */
#ifndef BOOT_META_H
#define BOOT_META_H

#include <stdint.h>
#include "boot_slot.h"

#define BOOT_META_MAGIC         (0xB007B007UL)
#define BOOT_META_VERSION       (1U)
#define BOOT_BOOT_TRIAL_MAX     (3U)

#define BOOT_META_FLAG_PENDING  (1U << 0)

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint8_t active_slot;
    uint8_t pending_slot;
    uint8_t boot_trial;
    uint8_t flags;
    uint32_t slot_crc[CM7_APP_SLOT_COUNT];
    uint32_t slot_size[CM7_APP_SLOT_COUNT];
    uint32_t crc32;
} boot_meta_t;

/**
 * @brief  初始化 BKPSRAM 硬件，使能备份域访问
 * @retval 无
 */
void boot_meta_hw_init(void);

/**
 * @brief  从 BKPSRAM 加载启动元数据，无效时填充默认值
 * @param  meta 输出元数据结构指针
 * @retval 0 成功，-1 参数无效
 */
int boot_meta_load(boot_meta_t *meta);

/**
 * @brief  将启动元数据写入 BKPSRAM 并更新 CRC
 * @param  meta 待保存的元数据结构指针
 * @retval 0 成功，-1 参数无效
 */
int boot_meta_save(const boot_meta_t *meta);

/**
 * @brief  根据元数据选择本次应启动的槽索引（pending 优先，否则 active）
 * @param  meta 当前元数据结构指针
 * @retval 启动槽索引
 */
uint8_t boot_meta_select_boot_slot(const boot_meta_t *meta);

/**
 * @brief  启动失败时递减试启动计数并清除待升级标志
 * @param  meta 元数据结构指针（会被修改并保存）
 * @retval 无
 */
void boot_meta_on_boot_failure(boot_meta_t *meta);

/**
 * @brief  确认待升级镜像已成功启动，提交 pending 状态
 * @param  meta 元数据结构指针（会被修改并保存）
 * @retval 无
 */
void boot_meta_commit_pending(boot_meta_t *meta);

/**
 * @brief  标记指定槽为待升级并记录镜像大小与 CRC
 * @param  meta         元数据结构指针（会被修改并保存）
 * @param  slot_index   待升级槽索引
 * @param  image_size   镜像字节长度
 * @param  image_crc32  镜像 CRC32 校验值
 * @retval 无
 */
void boot_meta_set_pending(boot_meta_t *meta, uint8_t slot_index,
                           uint32_t image_size, uint32_t image_crc32);

#endif /* BOOT_META_H */
