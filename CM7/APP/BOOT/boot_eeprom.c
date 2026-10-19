/**
 * @file    boot_eeprom.c
 * @brief   EEPROM 双备份启动元数据实现（CM7 + CM4）
 */
#include "boot_eeprom.h"
#include <string.h>
#include "bl24c16f.h"
#include "bsp_eeprom.h"
#include "mylog.h"
#include "freertos.h"
#include "task.h"
#include "cmsis_os2.h"
#include "iwdg.h"

#define BOOT_EE_TRIAL_MAX (3U)

/** struct_ver=1 布局（升级前），仅用于迁移读取 */
typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint16_t struct_ver;
    uint16_t length;
    uint8_t confirmed_slot;
    uint8_t pending_slot;
    uint8_t boot_trial;
    uint8_t flags;
    uint32_t app_version[CM7_APP_SLOT_COUNT];
    uint32_t app_size[CM7_APP_SLOT_COUNT];
    uint32_t app_crc[CM7_APP_SLOT_COUNT];
    uint32_t seq;
    uint32_t crc32;
} boot_eeprom_meta_v1_t;

static BL24C16F_HandleTypeDef s_ee_dev;
static uint8_t s_ee_ready;

/**
 * @brief  切换槽位线程
 * @note 该线程用于测试切换当前运行的槽位（A 或 B）。
 */
static void SwitchSlot_Thread(void *argument)
{
    static boot_eeprom_meta_t meta;
    uint8_t armed = 0U;
    uint8_t stable = 0U;
    (void)argument;

    LOG_INFO("CM7-Slot-A/B", "KEY0 ready, running_slot=%u base=0x%08lX",
             (unsigned int)boot_get_running_slot(),
             (unsigned long)boot_slot_base_get(boot_get_running_slot()));
    for (;;)
    {
        if (HAL_GPIO_ReadPin(KEY0_GPIO_Port, KEY0_Pin) == GPIO_PIN_RESET)
        {
            if (stable < 3U)
            {
                stable++;
            }
            if (stable == 3U)
            {
                armed = 1U;
            }
        }
        else
        {
            stable = 0U;
            if (armed != 0U)
            {
                osDelay(40U);
                if (HAL_GPIO_ReadPin(KEY0_GPIO_Port, KEY0_Pin) == GPIO_PIN_SET)
                {
                    uint8_t running_slot = boot_get_running_slot();
                    uint8_t target = (running_slot == CM7_APP_SLOT_A_INDEX) ? CM7_APP_SLOT_B_INDEX : CM7_APP_SLOT_A_INDEX;
                    uint32_t size = CM7_APP_SLOT_A_SIZE;
                    uint32_t crc = 0xFFFFFFFFU;
                    const uint8_t *image = (const uint8_t *)boot_slot_base_get(target);
                    armed = 0U;
                    if ((boot_slot_index_is_valid(running_slot) == 0U) ||
                        (boot_eeprom_load(&meta) != 0) ||
                        ((meta.flags & BOOT_EE_FLAG_PENDING) != 0U) ||
                        ((meta.cm4_flags & BOOT_EE_FLAG_CM4_PENDING) != 0U) ||
                        (boot_slot_is_valid(target) == 0))
                    {
                        LOG_ERROR("CM7-Slot-A/B", "switch rejected: check EEPROM, pending and target vector");
                        continue;
                    }
                    /* ST-Link 重刷镜像后旧 CRC 可能失效，按启动选择结果同步测试基线。 */
                    if (meta.confirmed_slot != running_slot)
                    {
                        if (boot_eeprom_select_boot_slot(&meta) != running_slot)
                        {
                            LOG_ERROR("CM7-Slot-A/B", "baseline running_slot mismatch");
                            continue;
                        }
                        meta.confirmed_slot = running_slot;
                        if (boot_eeprom_save(&meta) != 0)
                        {
                            LOG_ERROR("CM7-Slot-A/B", "baseline sync FAIL");
                            continue;
                        }
                        LOG_INFO("CM7-Slot-A/B", "baseline synced: confirmed=%u", (unsigned int)running_slot);
                    }
                    for (uint32_t i = 0U; i < size; i++)
                    {
                        crc ^= image[i];
                        for (uint32_t bit = 0U; bit < 8U; bit++)
                        {
                            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
                        }
                        if ((i & 4095U) == 0U)
                        {
                            HAL_IWDG_Refresh(&hiwdg1);
                            osDelay(1U);
                        }
                    }
                    crc = ~crc;
                    LOG_INFO("CM7-Slot-A/B", "switch %u -> %u base=0x%08lX full_slot_size=%lu crc=0x%08lX",
                             (unsigned int)running_slot, (unsigned int)target,
                             (unsigned long)boot_slot_base_get(target), (unsigned long)size, (unsigned long)crc);
                    if (boot_eeprom_set_pending(&meta, target, size, crc, meta.app_version[target]) != 0)
                    {
                        LOG_ERROR("CM7-Slot-A/B", "pending save FAIL");
                        continue;
                    }
                    LOG_INFO("CM7-Slot-A/B", "pending saved: slot=%u trial=%u seq=%lu; reboot",
                             (unsigned int)meta.pending_slot, (unsigned int)meta.boot_trial, (unsigned long)meta.seq);
                    osDelay(100U);
                    HAL_NVIC_SystemReset();
                }
            }
        }
        osDelay(20U);
    }
}

void EEPROM_Thread_Start(void)
{
    BaseType_t xReturn = pdPASS;
    xReturn = xTaskCreate((TaskFunction_t)SwitchSlot_Thread,
                          (char *)"SwitchSlot_Thread",
                          (uint16_t)512,
                          (void *)NULL,
                          (uint8_t)5,
                          (TaskHandle_t *)NULL);
    if (xReturn != pdPASS)
    {
        return;
    }
}

/**
 * @brief  计算一段字节数据的 CRC32（多项式 0xEDB88320）
 * @param  data   待校验数据
 * @param  length 数据字节数
 * @retval CRC32 校验值
 */
static uint32_t boot_ee_crc32_bytes(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFU;

    for (uint32_t i = 0U; i < length; i++)
    {
        crc ^= data[i];
        for (uint32_t bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 1U) != 0U)
            {
                crc = (crc >> 1) ^ 0xEDB88320U;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return ~crc;
}

/**
 * @brief  校验镜像长度、向量表及实际 Flash 内容的 CRC32。
 * @param  slot   待校验的槽位（A/B）
 * @param  cm4    是否为 CM4 槽
 * @param  size   镜像尺寸（字节）
 * @param  crc    镜像 CRC32 校验值
 * @param  pending 是否为 pending 槽
 * @retval 1 校验通过，0 校验失败
 */
static int boot_ee_image_valid(uint8_t slot, int cm4, uint32_t size,
                               uint32_t crc, int pending)
{
    uint32_t base = cm4 ? cm4_boot_slot_base_get(slot) : boot_slot_base_get(slot);
    uint32_t end = cm4 ? cm4_boot_slot_end_get(slot) : boot_slot_end_get(slot);

    if ((base == 0U) ||
        ((cm4 ? cm4_boot_slot_is_valid(slot) : boot_slot_is_valid(slot)) == 0))
    {
        return 0;
    }
    /* 兼容尚未登记长度/CRC 的出厂基线，但 pending 必须完整校验。 */
    if ((pending == 0) && (size == 0U) && (crc == 0U))
    {
        return 1;
    }
    if ((size < 8U) || (size > end - base) ||
        ((*((volatile uint32_t *)(base + 4U)) & ~1UL) >= base + size))
    {
        return 0;
    }
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
    {
        SCB_InvalidateDCache_by_Addr((void *)base, (int32_t)((size + 31U) & ~31UL));
    }
#endif
    return boot_ee_crc32_bytes((const uint8_t *)base, size) == crc;
}

/**
 * @brief  计算 v2 元数据 CRC32，不含 crc32 字段自身。
 * @param  meta 待校验的 v2 元数据
 * @retval CRC32 校验值
 */
static uint32_t boot_ee_crc32(const boot_eeprom_meta_t *meta)
{
    return boot_ee_crc32_bytes((const uint8_t *)meta,
                               sizeof(boot_eeprom_meta_t) - sizeof(uint32_t));
}

/**
 * @brief  计算 v1 旧版元数据的 CRC32（不含末尾 crc32 字段自身）
 * @param  meta 待校验的 v1 元数据
 * @retval CRC32 校验值
 */
static uint32_t boot_ee_crc32_v1(const boot_eeprom_meta_v1_t *meta)
{
    return boot_ee_crc32_bytes((const uint8_t *)meta,
                               sizeof(boot_eeprom_meta_v1_t) - sizeof(uint32_t));
}

/**
 * @brief  填入 CM4 槽默认元数据（confirmed=A，无 pending，版本/尺寸/CRC 清零）
 * @param  meta 待填充的元数据
 */
static void boot_ee_init_cm4_defaults(boot_eeprom_meta_t *meta)
{
    meta->cm4_confirmed_slot = CM4_APP_SLOT_A_INDEX;
    meta->cm4_pending_slot = CM4_APP_SLOT_A_INDEX;
    meta->cm4_boot_trial = 0U;
    meta->cm4_flags = 0U;
    memset(meta->cm4_app_version, 0, sizeof(meta->cm4_app_version));
    memset(meta->cm4_app_size, 0, sizeof(meta->cm4_app_size));
    memset(meta->cm4_app_crc, 0, sizeof(meta->cm4_app_crc));
}

/**
 * @brief  填入整份默认元数据（magic/结构版本/双核 confirmed=A，seq=1 并计算 CRC）
 * @param  meta 待填充的元数据
 */
static void boot_ee_set_defaults(boot_eeprom_meta_t *meta)
{
    memset(meta, 0, sizeof(*meta));
    meta->magic = BOOT_EE_MAGIC;
    meta->struct_ver = BOOT_EE_STRUCT_VER;
    meta->length = (uint16_t)sizeof(boot_eeprom_meta_t);
    meta->confirmed_slot = CM7_APP_SLOT_A_INDEX;
    meta->pending_slot = CM7_APP_SLOT_A_INDEX;
    meta->boot_trial = 0U;
    meta->flags = 0U;
    boot_ee_init_cm4_defaults(meta);
    meta->seq = 1U;
    meta->crc32 = boot_ee_crc32(meta);
}

/**
 * @brief  将 v1 元数据升级为 v2：保留 CM7 字段与 seq，CM4 部分取默认值并重算 CRC
 * @param  v1  输入的 v1 记录
 * @param  out 输出的 v2 记录
 */
static void boot_ee_upgrade_v1_to_v2(const boot_eeprom_meta_v1_t *v1,
                                     boot_eeprom_meta_t *out)
{
    memset(out, 0, sizeof(*out));
    out->magic = BOOT_EE_MAGIC;
    out->struct_ver = BOOT_EE_STRUCT_VER;
    out->length = (uint16_t)sizeof(boot_eeprom_meta_t);
    out->confirmed_slot = v1->confirmed_slot;
    out->pending_slot = v1->pending_slot;
    out->boot_trial = v1->boot_trial;
    out->flags = v1->flags;
    memcpy(out->app_version, v1->app_version, sizeof(out->app_version));
    memcpy(out->app_size, v1->app_size, sizeof(out->app_size));
    memcpy(out->app_crc, v1->app_crc, sizeof(out->app_crc));
    boot_ee_init_cm4_defaults(out);
    out->seq = v1->seq;
    out->crc32 = boot_ee_crc32(out);
}

/**
 * @brief  校验内存中一份 v2 元数据：magic/结构版本/长度/槽号边界/CRC32
 * @param  meta 待校验的元数据
 * @retval 1 合法，0 非法
 */
static int boot_ee_copy_valid_v2(const boot_eeprom_meta_t *meta)
{
    if (meta->magic != BOOT_EE_MAGIC)
    {
        return 0;
    }
    if (meta->struct_ver != BOOT_EE_STRUCT_VER)
    {
        return 0;
    }
    if (meta->length != (uint16_t)sizeof(boot_eeprom_meta_t))
    {
        return 0;
    }
    if (meta->confirmed_slot >= CM7_APP_SLOT_COUNT)
    {
        return 0;
    }
    if (meta->pending_slot >= CM7_APP_SLOT_COUNT)
    {
        return 0;
    }
    if ((meta->cm4_confirmed_slot != CM4_APP_SLOT_A_INDEX) &&
        (meta->cm4_confirmed_slot != CM4_APP_SLOT_B_INDEX))
    {
        return 0;
    }
    if ((meta->cm4_pending_slot != CM4_APP_SLOT_A_INDEX) &&
        (meta->cm4_pending_slot != CM4_APP_SLOT_B_INDEX))
    {
        return 0;
    }
    if (meta->crc32 != boot_ee_crc32(meta))
    {
        return 0;
    }
    return 1;
}

/**
 * @brief  校验内存中一份 v1 旧版元数据（用于升级前的读取判定）
 * @param  meta 待校验的元数据
 * @retval 1 合法，0 非法
 */
static int boot_ee_copy_valid_v1(const boot_eeprom_meta_v1_t *meta)
{
    if (meta->magic != BOOT_EE_MAGIC)
    {
        return 0;
    }
    if (meta->struct_ver != BOOT_EE_STRUCT_VER_V1)
    {
        return 0;
    }
    if (meta->length != (uint16_t)sizeof(boot_eeprom_meta_v1_t))
    {
        return 0;
    }
    if (meta->confirmed_slot >= CM7_APP_SLOT_COUNT)
    {
        return 0;
    }
    if (meta->pending_slot >= CM7_APP_SLOT_COUNT)
    {
        return 0;
    }
    if (meta->crc32 != boot_ee_crc32_v1(meta))
    {
        return 0;
    }
    return 1;
}

/**
 * @brief  从 EEPROM 指定地址读出一段原始数据
 * @param  addr 起始地址
 * @param  buf  输出缓冲区
 * @param  len  读取字节数
 * @retval 0 成功，-1 读取失败
 */
static int boot_ee_read_raw(uint16_t addr, uint8_t *buf, uint16_t len)
{
    if (BL24C16F_Read(&s_ee_dev, addr, buf, len) != HAL_OK)
    {
        return -1;
    }
    return 0;
}

/**
 * @brief  读一份备份：优先 v2，否则尝试 v1 并升级到内存中的 v2
 * @retval 1 有效，0 无效
 */
static int boot_ee_read_copy_as_v2(uint16_t addr, boot_eeprom_meta_t *out)
{
    uint8_t raw[BOOT_EE_COPY_SIZE];
    boot_eeprom_meta_t v2;
    boot_eeprom_meta_v1_t v1;

    /* 结构放不进单份备份区时直接判无效 */
    if (sizeof(boot_eeprom_meta_t) > BOOT_EE_COPY_SIZE)
    {
        return 0;
    }

    memset(raw, 0, sizeof(raw));
    if (boot_ee_read_raw(addr, raw, (uint16_t)sizeof(raw)) != 0)
    {
        return 0;
    }

    memcpy(&v2, raw, sizeof(v2));
    if (boot_ee_copy_valid_v2(&v2) != 0)
    {
        *out = v2;
        return 1;
    }

    /* 旧版记录：升级为 v2 返回，仅在内存中转换，不回写 EEPROM */
    memcpy(&v1, raw, sizeof(v1));
    if (boot_ee_copy_valid_v1(&v1) != 0)
    {
        boot_ee_upgrade_v1_to_v2(&v1, out);
        return 1;
    }

    return 0;
}

/**
 * @brief  初始化就绪标志并打印双备份地址（I2C 驱动由 HAL 侧完成初始化）
 * @retval 0 成功
 */
int boot_eeprom_init(void)
{
    s_ee_dev = *bsp_eeprom_handle();
    s_ee_ready = 1U;
    LOG_INFO("APP", "Boot EEPROM ready (dual copy @ 0x%04X / 0x%04X, ver=%u)",
             (unsigned int)BOOT_EE_COPY0_ADDR,
             (unsigned int)BOOT_EE_COPY1_ADDR,
             (unsigned int)BOOT_EE_STRUCT_VER);
    return 0;
}

/**
 * @brief  加载元数据：优先取两份备份中 seq 更大的合法份；均无效则填默认值
 * @param  meta 输出元数据（无论成败都会被填充）
 * @retval 0 读到合法备份，1 使用默认值（含未就绪场景），-1 参数错误
 */
int boot_eeprom_load(boot_eeprom_meta_t *meta)
{
    boot_eeprom_meta_t c0;
    boot_eeprom_meta_t c1;
    int c0_ok = 0;
    int c1_ok = 0;

    if (meta == NULL)
    {
        return -1;
    }

    /* 先填默认值：后续任何失败路径都保证 meta 有合法内容 */
    boot_ee_set_defaults(meta);

    /* 未初始化时保留默认值 */
    if (s_ee_ready == 0U)
    {
        return 1;
    }

    c0_ok = boot_ee_read_copy_as_v2(BOOT_EE_COPY0_ADDR, &c0);
    c1_ok = boot_ee_read_copy_as_v2(BOOT_EE_COPY1_ADDR, &c1);
    LOG_INFO("APP", "EEPROM copy0 valid=%d seq=%lu | copy1 valid=%d seq=%lu",
             c0_ok, c0_ok ? (unsigned long)c0.seq : 0UL,
             c1_ok, c1_ok ? (unsigned long)c1.seq : 0UL);

    /* 双份均有效：取 seq 更大（更新）的一份 */
    if ((c0_ok != 0) && (c1_ok != 0))
    {
        *meta = (c0.seq >= c1.seq) ? c0 : c1;
        return 0;
    }
    if (c0_ok != 0)
    {
        *meta = c0;
        return 0;
    }
    if (c1_ok != 0)
    {
        *meta = c1;
        return 0;
    }

    LOG_INFO("APP", "EEPROM meta empty/invalid, use defaults (CM7/CM4 confirmed=A)");
    return 1;
}

/**
 * @brief  保存元数据：seq 自增并重算 CRC，轮换写入较旧或无效的那份备份
 * @param  meta 待保存的元数据
 * @retval 0 成功，-1 失败（未就绪/结构超长/写入失败）
 */
int boot_eeprom_save(boot_eeprom_meta_t *meta)
{
    boot_eeprom_meta_t c0;
    boot_eeprom_meta_t c1;
    uint16_t target;
    int c0_ok = 0;
    int c1_ok = 0;

    if ((meta == NULL) || (s_ee_ready == 0U))
    {
        return -1;
    }

    if (sizeof(boot_eeprom_meta_t) > BOOT_EE_COPY_SIZE)
    {
        LOG_ERROR("APP", "EEPROM meta too large (%u > %u)",
                  (unsigned int)sizeof(boot_eeprom_meta_t),
                  (unsigned int)BOOT_EE_COPY_SIZE);
        return -1;
    }

    /* 强制刷新头字段，seq 自增并重算 CRC（seq 用于双备份新旧裁决） */
    meta->magic = BOOT_EE_MAGIC;
    meta->struct_ver = BOOT_EE_STRUCT_VER;
    meta->length = (uint16_t)sizeof(boot_eeprom_meta_t);
    meta->seq++;
    meta->crc32 = boot_ee_crc32(meta);

    /* 轮换写入：优先覆盖 seq 更旧或无效的那一份 */
    c0_ok = boot_ee_read_copy_as_v2(BOOT_EE_COPY0_ADDR, &c0);
    c1_ok = boot_ee_read_copy_as_v2(BOOT_EE_COPY1_ADDR, &c1);

    if ((c0_ok != 0) && (c1_ok != 0))
    {
        target = (c0.seq <= c1.seq) ? BOOT_EE_COPY0_ADDR : BOOT_EE_COPY1_ADDR;
    }
    else if (c0_ok == 0)
    {
        target = BOOT_EE_COPY0_ADDR;
    }
    else
    {
        target = BOOT_EE_COPY1_ADDR;
    }

    if (boot_ee_copy_valid_v2(meta) == 0)
    {
        return -1;
    }
    if (BL24C16F_Write(&s_ee_dev, target, (const uint8_t *)meta, sizeof(*meta)) != HAL_OK)
    {
        LOG_ERROR("APP", "EEPROM save failed @ 0x%04X", (unsigned int)target);
        return -1;
    }

    if ((boot_ee_read_raw(target, (uint8_t *)&c0, sizeof(c0)) != 0) ||
        (boot_ee_copy_valid_v2(&c0) == 0) ||
        (memcmp(&c0, meta, sizeof(c0)) != 0))
    {
        LOG_ERROR("APP", "EEPROM readback failed @ 0x%04X", (unsigned int)target);
        return -1;
    }
    return 0;
}

/**
 * @brief  选择 CM7 启动槽：pending（需置位且有效）→ confirmed → Slot A → Slot B
 * @param  meta 元数据，可为 NULL
 * @retval 槽索引，meta 为 NULL 时返回 Slot A
 */
uint8_t boot_eeprom_select_boot_slot(const boot_eeprom_meta_t *meta)
{
    if (meta == NULL)
    {
        return CM7_APP_SLOT_A_INDEX;
    }

    /* pending 需同时满足：标志置位、索引在边界内、槽内镜像校验有效 */
    if (((meta->flags & BOOT_EE_FLAG_PENDING) != 0U) &&
        (meta->pending_slot < CM7_APP_SLOT_COUNT) &&
        (meta->boot_trial > 0U) &&
        (boot_ee_image_valid(meta->pending_slot, 0, meta->app_size[meta->pending_slot],
                             meta->app_crc[meta->pending_slot], 1) != 0))
    {
        return meta->pending_slot;
    }

    if ((meta->confirmed_slot < CM7_APP_SLOT_COUNT) &&
        ((meta->flags & BOOT_EE_FLAG_PENDING) == 0U || meta->confirmed_slot != meta->pending_slot) &&
        (boot_ee_image_valid(meta->confirmed_slot, 0, meta->app_size[meta->confirmed_slot],
                             meta->app_crc[meta->confirmed_slot], 0) != 0))
    {
        return meta->confirmed_slot;
    }

    /* confirmed 也不可用：按 Slot A → Slot B 兜底 */
    if ((meta->pending_slot != CM7_APP_SLOT_A_INDEX || (meta->flags & BOOT_EE_FLAG_PENDING) == 0U) &&
        boot_ee_image_valid(0U, 0, meta->app_size[0], meta->app_crc[0], 0))
    {
        return CM7_APP_SLOT_A_INDEX;
    }

    if ((meta->pending_slot != CM7_APP_SLOT_B_INDEX || (meta->flags & BOOT_EE_FLAG_PENDING) == 0U) &&
        boot_ee_image_valid(1U, 0, meta->app_size[1], meta->app_crc[1], 0))
    {
        return CM7_APP_SLOT_B_INDEX;
    }

    return 255U;
}

/**
 * @brief  CM7 试启动处理：从 pending 槽启动前递减 boot_trial，用尽则清 pending
 *         回退 confirmed 并落盘
 * @param  meta 元数据，可为 NULL
 * @retval 最终应启动的槽索引
 */
uint8_t boot_eeprom_prepare_boot(boot_eeprom_meta_t *meta)
{
    uint8_t slot;

    if (meta == NULL)
    {
        return CM7_APP_SLOT_A_INDEX;
    }

    slot = boot_eeprom_select_boot_slot(meta);

    if ((meta->flags & BOOT_EE_FLAG_PENDING) != 0U)
    {
        if ((slot == meta->pending_slot) && (meta->boot_trial > 0U))
        {
            meta->boot_trial--;
            if (boot_eeprom_save(meta) == 0)
            {
                return slot;
            }
            LOG_ERROR("APP", "CM7 trial save failed, fallback");
        }
        /* 保留被拒候选的排除条件，避免兜底再次选回该槽。 */
        meta->boot_trial = 0U;
        slot = boot_eeprom_select_boot_slot(meta);
        meta->flags &= (uint8_t)~BOOT_EE_FLAG_PENDING;
        meta->pending_slot = meta->confirmed_slot;
        if (boot_eeprom_save(meta) != 0)
        {
            LOG_ERROR("APP", "CM7 pending clear save failed");
        }
    }

    return slot;
}

/**
 * @brief  OTA 成功后由 APP 调用：pending 槽转正为 confirmed，清 trial/pending 标志并保存
 * @param  meta 元数据，可为 NULL（无 pending 时直接返回）
 * @retval 0 成功，-1 失败
 */
int boot_eeprom_commit_pending(boot_eeprom_meta_t *meta)
{
    if ((meta == NULL) || ((meta->flags & BOOT_EE_FLAG_PENDING) == 0U))
    {
        return -1;
    }

    meta->confirmed_slot = meta->pending_slot;
    meta->boot_trial = 0U;
    meta->flags &= (uint8_t)~BOOT_EE_FLAG_PENDING;

    int ret = boot_eeprom_save(meta);
    if (ret != 0)
    {
        return ret;
    }

    return 0;
}

/**
 * @brief  标记 CM7 pending 槽（OTA 写完镜像后由 APP 调用）：记录镜像信息，
 *         置试启动次数与 pending 标志后保存
 * @param  meta       元数据
 * @param  slot_index 待启动槽索引
 * @param  image_size 镜像大小
 * @param  image_crc32 镜像 CRC32
 * @param  version    镜像版本号
 * @retval 0 成功，-1 失败
 */
int boot_eeprom_set_pending(boot_eeprom_meta_t *meta, uint8_t slot_index,
                            uint32_t image_size, uint32_t image_crc32,
                            uint32_t version)
{
    boot_eeprom_meta_t previous;
    if ((meta == NULL) || (slot_index >= CM7_APP_SLOT_COUNT) ||
        (slot_index == meta->confirmed_slot) ||
        !boot_ee_image_valid(slot_index, 0, image_size, image_crc32, 1))
    {
        return -1;
    }

    previous = *meta;
    meta->pending_slot = slot_index;
    meta->app_size[slot_index] = image_size;
    meta->app_crc[slot_index] = image_crc32;
    meta->app_version[slot_index] = version;
    /* 预留 BOOT_EE_TRIAL_MAX 次试启动，期间未 commit 则自动回退 confirmed */
    meta->boot_trial = BOOT_EE_TRIAL_MAX;
    meta->flags |= BOOT_EE_FLAG_PENDING;

    int ret = boot_eeprom_save(meta);
    if (ret != 0)
    {
        *meta = previous;
        return ret;
    }

    return 0;
}

/**
 * @brief  选择 CM4 启动槽：pending（需置位且有效）→ confirmed → Slot A → Slot B
 * @param  meta 元数据，可为 NULL
 * @retval 槽索引，meta 为 NULL 时返回 Slot A
 */
uint8_t boot_eeprom_select_cm4_boot_slot(const boot_eeprom_meta_t *meta)
{
    if (meta == NULL)
    {
        return CM4_APP_SLOT_A_INDEX;
    }

    /* pending 需同时满足：标志置位、索引在边界内、槽内镜像校验有效 */
    if (((meta->cm4_flags & BOOT_EE_FLAG_CM4_PENDING) != 0U) &&
        ((meta->cm4_pending_slot == CM4_APP_SLOT_A_INDEX) ||
         (meta->cm4_pending_slot == CM4_APP_SLOT_B_INDEX)) &&
        (meta->cm4_boot_trial > 0U) &&
        boot_ee_image_valid(meta->cm4_pending_slot, 1, meta->cm4_app_size[meta->cm4_pending_slot],
                            meta->cm4_app_crc[meta->cm4_pending_slot], 1))
    {
        return meta->cm4_pending_slot;
    }

    if (((meta->cm4_confirmed_slot == CM4_APP_SLOT_A_INDEX) ||
         (meta->cm4_confirmed_slot == CM4_APP_SLOT_B_INDEX)) &&
        ((meta->cm4_flags & BOOT_EE_FLAG_CM4_PENDING) == 0U || meta->cm4_confirmed_slot != meta->cm4_pending_slot) &&
        boot_ee_image_valid(meta->cm4_confirmed_slot, 1, meta->cm4_app_size[meta->cm4_confirmed_slot],
                            meta->cm4_app_crc[meta->cm4_confirmed_slot], 0))
    {
        return meta->cm4_confirmed_slot;
    }

    /* confirmed 也不可用：按 Slot A → Slot B 兜底 */
    if ((meta->cm4_pending_slot != 0U || (meta->cm4_flags & BOOT_EE_FLAG_CM4_PENDING) == 0U) &&
        boot_ee_image_valid(0U, 1, meta->cm4_app_size[0], meta->cm4_app_crc[0], 0))
    {
        return CM4_APP_SLOT_A_INDEX;
    }

    if ((meta->cm4_pending_slot != 1U || (meta->cm4_flags & BOOT_EE_FLAG_CM4_PENDING) == 0U) &&
        boot_ee_image_valid(1U, 1, meta->cm4_app_size[1], meta->cm4_app_crc[1], 0))
    {
        return CM4_APP_SLOT_B_INDEX;
    }

    return 255U;
}

/**
 * @brief  CM4 试启动处理：从 pending 槽启动前递减 cm4_boot_trial，用尽则清
 *         pending 回退 confirmed 并落盘
 * @param  meta 元数据，可为 NULL
 * @retval 最终应启动的 CM4 槽索引
 */
uint8_t boot_eeprom_prepare_cm4_boot(boot_eeprom_meta_t *meta)
{
    uint8_t slot;

    if (meta == NULL)
    {
        return CM4_APP_SLOT_A_INDEX;
    }

    slot = boot_eeprom_select_cm4_boot_slot(meta);

    if ((meta->cm4_flags & BOOT_EE_FLAG_CM4_PENDING) != 0U)
    {
        if ((slot == meta->cm4_pending_slot) && (meta->cm4_boot_trial > 0U))
        {
            meta->cm4_boot_trial--;
            if (boot_eeprom_save(meta) == 0)
            {
                return slot;
            }
            LOG_ERROR("APP", "CM4 trial save failed, fallback");
        }
        meta->cm4_boot_trial = 0U;
        slot = boot_eeprom_select_cm4_boot_slot(meta);
        meta->cm4_flags &= (uint8_t)~BOOT_EE_FLAG_CM4_PENDING;
        meta->cm4_pending_slot = meta->cm4_confirmed_slot;
        if (boot_eeprom_save(meta) != 0)
        {
            LOG_ERROR("APP", "CM4 pending clear save failed");
        }
    }

    return slot;
}

/**
 * @brief  CM4 OTA 成功后：pending 槽转正为 confirmed，清 trial/pending 标志并保存
 * @param  meta 元数据，可为 NULL（无 pending 时直接返回）
 * @retval 0 成功，-1 失败
 */
int boot_eeprom_commit_cm4_pending(boot_eeprom_meta_t *meta)
{
    if ((meta == NULL) || ((meta->cm4_flags & BOOT_EE_FLAG_CM4_PENDING) == 0U))
    {
        return -1;
    }

    meta->cm4_confirmed_slot = meta->cm4_pending_slot;
    meta->cm4_boot_trial = 0U;
    meta->cm4_flags &= (uint8_t)~BOOT_EE_FLAG_CM4_PENDING;

    int ret = boot_eeprom_save(meta);
    if (ret != 0)
    {
        return ret;
    }

    return 0;
}

/**
 * @brief  标记 CM4 pending 槽：记录镜像信息，置试启动次数与 pending 标志后保存
 * @param  meta        元数据
 * @param  slot_index  待启动槽索引
 * @param  image_size  镜像大小
 * @param  image_crc32 镜像 CRC32
 * @param  version     镜像版本号
 * @retval 0 成功，-1 失败
 */
int boot_eeprom_set_cm4_pending(boot_eeprom_meta_t *meta, uint8_t slot_index,
                                uint32_t image_size, uint32_t image_crc32,
                                uint32_t version)
{
    boot_eeprom_meta_t previous;
    if ((meta == NULL) ||
        ((slot_index != CM4_APP_SLOT_A_INDEX) && (slot_index != CM4_APP_SLOT_B_INDEX)) ||
        (slot_index == meta->cm4_confirmed_slot) ||
        !boot_ee_image_valid(slot_index, 1, image_size, image_crc32, 1))
    {
        return -1;
    }

    previous = *meta;
    meta->cm4_pending_slot = slot_index;
    meta->cm4_app_size[slot_index] = image_size;
    meta->cm4_app_crc[slot_index] = image_crc32;
    meta->cm4_app_version[slot_index] = version;
    /* 预留 BOOT_EE_TRIAL_MAX 次试启动，期间未 commit 则自动回退 confirmed */
    meta->cm4_boot_trial = BOOT_EE_TRIAL_MAX;
    meta->cm4_flags |= BOOT_EE_FLAG_CM4_PENDING;

    int ret = boot_eeprom_save(meta);
    if (ret != 0)
    {
        *meta = previous;
        return ret;
    }

    return 0;
}
