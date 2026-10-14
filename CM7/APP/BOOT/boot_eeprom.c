/**
 * @file    boot_eeprom.c
 * @brief   EEPROM 双备份启动元数据实现（CM7 + CM4）
 */
#include "boot_eeprom.h"

#include <string.h>
#include <stdio.h>

#include "bl24c16f.h"
#include "bsp_eeprom.h"
#include "i2c.h"
#include "mylog.h"

#define BOOT_EE_TRIAL_MAX (3U) // 试启动额度：pending 槽最多连续尝试 3 次

/**
 * @brief  struct_ver=1 元数据结构体（升级前），仅用于迁移读取
 */
typedef struct __attribute__((packed))
{
    /* Information */
    uint32_t magic;      // 魔数，用于识别元数据结构
    uint16_t struct_ver; // 结构版本号，用于识别元数据结构
    uint16_t length;     // 元数据总长度，包含 CRC32 校验值
    /* A/B */
    uint8_t active_slot;                      // 当前活动槽槽号
    uint8_t pending_slot;                     // 待启动槽槽号，0 表示无 pending
    uint8_t boot_trial;                       // 启动尝试次数，0 表示未尝试
    uint8_t flags;                            // 标志位，0 表示无特殊标志
    uint32_t app_version[CM7_APP_SLOT_COUNT]; // 应用版本号数组
    uint32_t app_size[CM7_APP_SLOT_COUNT];    // 应用尺寸数组
    uint32_t app_crc[CM7_APP_SLOT_COUNT];     // 应用 CRC32 校验值数组
    /* Global */
    uint32_t seq;   // 序列号，用于识别元数据版本
    uint32_t crc32; // CRC32 校验值，用于验证元数据完整性
} boot_eeprom_meta_v1_t;

static BL24C16F_HandleTypeDef s_ee_dev; // EEPROM 设备句柄，由 boot_eeprom_init 从 BSP 层拷贝
static uint8_t s_ee_ready;              // 就绪标志：1 已初始化，0 未初始化（load 填默认、save 直接拒绝）

/**
 * @brief  计算一段字节数据的 CRC32（多项式 0xEDB88320）
 * @param  data   待校验数据
 * @param  length 数据字节数
 * @retval CRC32 校验值
 */
static uint32_t boot_ee_crc32_bytes(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFU; // CRC32 标准初值（全 1）

    for (uint32_t i = 0U; i < length; i++) // 逐字节喂入待校验数据
    {
        crc ^= data[i];                          // 当前字节异或进 CRC 低位
        for (uint32_t bit = 0U; bit < 8U; bit++) // 每字节按位处理 8 次
        {
            if ((crc & 1U) != 0U) // 最低位为 1：右移后需异或多项式
            {
                crc = (crc >> 1) ^ 0xEDB88320U; // 反射形式多项式（zlib/以太网标准）
            }
            else // 最低位为 0：仅右移，不异或
            {
                crc >>= 1; // 逻辑右移补 0
            }
        }
    }

    return ~crc; // 全部取反，得到标准 CRC32 结果
}

/**
 * @brief  计算 v2 元数据的 CRC32（不含末尾 crc32 字段自身）
 * @param  meta 待校验的 v2 元数据
 * @retval CRC32 校验值
 */
static uint32_t boot_ee_crc32(const boot_eeprom_meta_t *meta)
{
    return boot_ee_crc32_bytes((const uint8_t *)meta,                          // 从结构体首字节起算
                               sizeof(boot_eeprom_meta_t) - sizeof(uint32_t)); // 到 crc32 字段自身之前为止
}

/**
 * @brief  计算 v1 旧版元数据的 CRC32（不含末尾 crc32 字段自身）
 * @param  meta 待校验的 v1 元数据
 * @retval CRC32 校验值
 */
static uint32_t boot_ee_crc32_v1(const boot_eeprom_meta_v1_t *meta)
{
    return boot_ee_crc32_bytes((const uint8_t *)meta,                             // 按 v1 结构体首字节起算
                               sizeof(boot_eeprom_meta_v1_t) - sizeof(uint32_t)); // 到 v1 的 crc32 字段之前为止
}

/**
 * @brief  填入 CM4 槽默认元数据（active=A，无 pending，版本/尺寸/CRC 清零）
 * @param  meta 待填充的元数据
 */
static void boot_ee_init_cm4_defaults(boot_eeprom_meta_t *meta)
{
    meta->cm4_active_slot = CM4_APP_SLOT_A_INDEX;                    // CM4 默认激活 A 槽
    meta->cm4_pending_slot = CM4_APP_SLOT_A_INDEX;                   // 无待启动槽，与 active 一致
    meta->cm4_boot_trial = 0U;                                       // 试启动次数清零
    meta->cm4_flags = 0U;                                            // 不置 CM4 PENDING 标志
    memset(meta->cm4_app_version, 0, sizeof(meta->cm4_app_version)); // 版本号清零
    memset(meta->cm4_app_size, 0, sizeof(meta->cm4_app_size));       // 镜像大小清零
    memset(meta->cm4_app_crc, 0, sizeof(meta->cm4_app_crc));         // 镜像 CRC 清零
}

/**
 * @brief  填入整份默认元数据（magic/结构版本/双核 active=A，seq=1 并计算 CRC）
 * @param  meta 待填充的元数据
 */
static void boot_ee_set_defaults(boot_eeprom_meta_t *meta)
{
    memset(meta, 0, sizeof(*meta));                      // 整体清零，避免残留脏数据
    meta->magic = BOOT_EE_MAGIC;                         // 写入识别魔数 "EEMA"
    meta->struct_ver = BOOT_EE_STRUCT_VER;               // 标记为当前 v2 结构
    meta->length = (uint16_t)sizeof(boot_eeprom_meta_t); // 记录结构体总长度
    meta->active_slot = CM7_APP_SLOT_A_INDEX;            // 默认激活 CM7 A 槽
    meta->pending_slot = CM7_APP_SLOT_A_INDEX;           // 无待启动槽，与 active 一致
    meta->boot_trial = 0U;                               // 试启动次数清零
    meta->flags = 0U;                                    // 不置任何标志（含 PENDING）
    boot_ee_init_cm4_defaults(meta);                     // CM4 部分同样填 A 槽默认值
    meta->seq = 1U;                                      // 初始序列号，作为双备份新旧裁决基准
    meta->crc32 = boot_ee_crc32(meta);                   // 按当前内容计算校验值
}

/**
 * @brief  将 v1 元数据升级为 v2：保留 CM7 字段与 seq，CM4 部分取默认值并重算 CRC
 * @param  v1  输入的 v1 记录
 * @param  out 输出的 v2 记录
 */
static void boot_ee_upgrade_v1_to_v2(const boot_eeprom_meta_v1_t *v1,
                                     boot_eeprom_meta_t *out)
{
    memset(out, 0, sizeof(*out));                                        // 清空输出记录
    out->magic = BOOT_EE_MAGIC;                                          // 填入魔数
    out->struct_ver = BOOT_EE_STRUCT_VER;                                // 填入结构版本号
    out->length = (uint16_t)sizeof(boot_eeprom_meta_t);                  // 填入长度
    out->active_slot = v1->active_slot;                                  // 填入当前活动槽槽号
    out->pending_slot = v1->pending_slot;                                // 填入待启动槽槽号
    out->boot_trial = v1->boot_trial;                                    // 填入启动尝试次数
    out->flags = v1->flags;                                              // 填入标志位
    memcpy(out->app_version, v1->app_version, sizeof(out->app_version)); // 填入应用版本号数组
    memcpy(out->app_size, v1->app_size, sizeof(out->app_size));          // 填入应用尺寸数组
    memcpy(out->app_crc, v1->app_crc, sizeof(out->app_crc));             // 填入应用 CRC32 校验值数组
    boot_ee_init_cm4_defaults(out);                                      // 填入 CM4 槽默认元数据
    out->seq = v1->seq;                                                  // 填入序列号
    out->crc32 = boot_ee_crc32(out);                                     // 填入 CRC32 校验值
}

/**
 * @brief  校验内存中一份 v2 元数据：magic/结构版本/长度/槽号边界/CRC32
 * @param  meta 待校验的元数据
 * @retval 1 合法，0 非法
 */
static int boot_ee_copy_valid_v2(const boot_eeprom_meta_t *meta)
{
    if (meta->magic != BOOT_EE_MAGIC) // 如果魔数不匹配，说明不是 v2 元数据
    {
        return 0; // 魔数非法
    }
    if (meta->struct_ver != BOOT_EE_STRUCT_VER) // 如果结构版本号不匹配，说明不是 v2 元数据
    {
        return 0; // 结构版本非法
    }
    if (meta->length != (uint16_t)sizeof(boot_eeprom_meta_t)) // 如果长度不匹配，说明不是 v2 元数据
    {
        return 0; // 长度非法
    }
    if (meta->active_slot >= CM7_APP_SLOT_COUNT) // 如果当前活动槽槽号超出范围，说明不是 v2 元数据
    {
        return 0; // active 槽号越界
    }
    if (meta->pending_slot >= CM7_APP_SLOT_COUNT) // 如果待启动槽槽号超出范围，说明不是 v2 元数据
    {
        return 0; // pending 槽号越界
    }
    if ((meta->cm4_active_slot != CM4_APP_SLOT_A_INDEX) &&
        (meta->cm4_active_slot != CM4_APP_SLOT_B_INDEX)) // 如果 CM4 活动槽槽号超出范围，说明不是 v2 元数据
    {
        return 0; // CM4 active 槽号非法
    }
    if ((meta->cm4_pending_slot != CM4_APP_SLOT_A_INDEX) &&
        (meta->cm4_pending_slot != CM4_APP_SLOT_B_INDEX)) // 如果 CM4 待启动槽槽号超出范围，说明不是 v2 元数据
    {
        return 0; // CM4 pending 槽号非法
    }
    if (meta->crc32 != boot_ee_crc32(meta)) // 如果 CRC32 校验值不匹配，说明不是 v2 元数据
    {
        return 0; // CRC32 校验失败（数据损坏）
    }
    return 1; // 如果所有校验都通过，说明是 v2 元数据，合法
}

/**
 * @brief  校验内存中一份 v1 旧版元数据（用于升级前的读取判定）
 * @param  meta 待校验的元数据
 * @retval 1 合法，0 非法
 */
static int boot_ee_copy_valid_v1(const boot_eeprom_meta_v1_t *meta)
{
    if (meta->magic != BOOT_EE_MAGIC) // 如果魔数不匹配，说明不是 v1 元数据
    {
        return 0; // 魔数非法
    }
    if (meta->struct_ver != BOOT_EE_STRUCT_VER_V1) // 如果结构版本号不匹配，说明不是 v1 元数据
    {
        return 0; // 结构版本不是 v1
    }
    if (meta->length != (uint16_t)sizeof(boot_eeprom_meta_v1_t)) // 如果长度不匹配，说明不是 v1 元数据
    {
        return 0; // 长度非法
    }
    if (meta->active_slot >= CM7_APP_SLOT_COUNT) // 如果当前活动槽槽号超出范围，说明不是 v1 元数据
    {
        return 0; // active 槽号越界
    }
    if (meta->pending_slot >= CM7_APP_SLOT_COUNT) // 如果待启动槽槽号超出范围，说明不是 v1 元数据
    {
        return 0; // pending 槽号越界
    }
    if (meta->crc32 != boot_ee_crc32_v1(meta)) // 如果 CRC32 校验值不匹配，说明不是 v1 元数据
    {
        return 0; // CRC32 校验失败（数据损坏）
    }
    return 1; // 如果所有校验都通过，说明是 v1 元数据，合法
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
    if (BL24C16F_Read(&s_ee_dev, addr, buf, len) != HAL_OK) // I2C 读取失败：总线/器件异常
    {
        return -1; // 报告读取失败，调用方按无效副本处理
    }
    return 0; // 读取成功，buf 中已是原始字节
}

/**
 * @brief  读一份备份的元数据（v2 或 v1）：优先 v2，否则尝试 v1 并升级到内存中的 v2
 * @param  addr  EEPROM 地址
 * @param  out    输出缓冲区
 * @retval 1 有效，0 无效
 */
static int boot_ee_read_copy_as_v2(uint16_t addr, boot_eeprom_meta_t *out)
{
    uint8_t raw[BOOT_EE_COPY_SIZE]; // 原始数据缓冲区
    boot_eeprom_meta_t v2;          // v2 元数据
    boot_eeprom_meta_v1_t v1;       // v1 元数据

    /* 结构放不进单份备份区时直接判无效 */
    if (sizeof(boot_eeprom_meta_t) > BOOT_EE_COPY_SIZE) // v2 元数据大小超过备份区大小
    {
        return 0; // 编译期布局不兼容，直接判无效
    }

    memset(raw, 0, sizeof(raw));                                 // 清空原始数据缓冲区
    if (boot_ee_read_raw(addr, raw, (uint16_t)sizeof(raw)) != 0) // 读取原始数据
    {
        return 0; // 读取失败，该副本按无效处理
    }

    memcpy(&v2, raw, sizeof(v2));        // 复制原始数据到 v2 元数据结构体
    if (boot_ee_copy_valid_v2(&v2) != 0) // 校验 v2 元数据
    {
        *out = v2; // 校验通过，输出 v2 元数据
        return 1;  // 副本有效
    }

    /* v2 校验失败，尝试 v1 元数据 */
    /* 尝试 v1 元数据：升级为 v2 返回，仅在内存中转换，不回写 EEPROM */
    memcpy(&v1, raw, sizeof(v1));        // 复制原始数据到 v1 元数据结构体
    if (boot_ee_copy_valid_v1(&v1) != 0) // 如果 v1 元数据校验通过，说明是 v1 元数据
    {
        boot_ee_upgrade_v1_to_v2(&v1, out); // 升级 v1 元数据为 v2 元数据，并且校验通过，输出 v2 元数据
        return 1;                           // 副本有效（已按 v2 语义返回）
    }

    return 0; // v2/v1 均校验失败：该副本无效（全新片或已损坏）
}

/**
 * @brief  初始化就绪标志并打印双备份地址（I2C 驱动由 HAL 侧完成初始化）
 * @retval 0 成功
 */
int boot_eeprom_init(void)
{
    s_ee_dev = *bsp_eeprom_handle(); // 获取 EEPROM 实例句柄
    s_ee_ready = 1U;                 // 标记 EEPROM 实例为就绪标志，1 表示就绪，0 表示未就绪
    LOG_INFO("BOOT", "EEPROM ready (dual copy @ 0x%04X / 0x%04X, ver=%u)",
             (unsigned int)BOOT_EE_COPY0_ADDR,
             (unsigned int)BOOT_EE_COPY1_ADDR,
             (unsigned int)BOOT_EE_STRUCT_VER);
    return 0; // 初始化成功
}

/**
 * @brief  加载元数据：优先取两份备份中 seq 更大的合法份；均无效则填默认值
 * @param  meta 输出元数据（无论成败都会被填充）
 * @retval 0 读到合法备份，1 使用默认值（含未就绪场景），-1 参数错误
 */
int boot_eeprom_load(boot_eeprom_meta_t *meta)
{
    boot_eeprom_meta_t c0; // 备份 0 地址元数据
    boot_eeprom_meta_t c1; // 备份 1 地址元数据
    int c0_ok = 0;         // 备份 0 地址有效标志，1 表示有效，0 表示无效
    int c1_ok = 0;         // 备份 1 地址有效标志，1 表示有效，0 表示无效

    /* 参数校验 */
    if (meta == NULL) // 空指针
    {
        return -1; // 参数错误，不填充
    }

    /* 先填默认值：后续任何失败路径都保证 meta 有合法内容 */
    boot_ee_set_defaults(meta); // 将 meta 填充默认值

    /* 未初始化时保留默认值 */
    if (s_ee_ready == 0U) // EEPROM 未初始化（init 失败或未调用）
    {
        return 1; // 按首次使用处理
    }

    /**
     * @brief  读取双份备份的元数据
     * @param  addr  EEPROM 地址
     * @param  out    输出缓冲区
     * @retval 1 有效，0 无效
     *
     * @note 假如 v2 校验通过，则说明当前读取的元数据是当前固件写的、含双核信息的完整元数据 v2 元数据，并将其返回。
     *          版本较新的元数据，直接原样采用，不做任何转换；
     *       假如 v1 校验通过，则说明当前读取的老版本固件遗留的旧格式元数据，只在内存里临时升级，不立刻回写 EEPROM，将其返回。
     *          老数据还躺在芯片里，等下次任何一次 save（比如首次落盘、试启动扣减）发生时，才会顺手以 v2 格式回写 EEPROM
     *
     * @note 最终都是要返回 v2 元数据，因为 v2 元数据带有 CM4 A/B 字段；
     *       而 v1 元数据只有 CM7 A/B 字段，故需要升级为 v2 元数据
     */
    c0_ok = boot_ee_read_copy_as_v2(BOOT_EE_COPY0_ADDR, &c0); // 读取 EEPROM 备份 0 地址有效的元数据，返回有效标志
    c1_ok = boot_ee_read_copy_as_v2(BOOT_EE_COPY1_ADDR, &c1); // 读取 EEPROM 备份 1 地址有效的元数据，返回有效标志

    /* 双份均有效：取 seq 更大（更新）的一份 */
    if ((c0_ok != 0) && (c1_ok != 0))
    {
        *meta = (c0.seq >= c1.seq) ? c0 : c1; // 取 seq 更大的合法份
        return 0;                             // 已读到合法备份
    }
    if (c0_ok != 0) // 如果备份 0 元数据有效
    {
        *meta = c0; // 取备份 0 元数据（副本 1 无效，下次 save 会修复它）
        return 0;   // 已读到合法备份
    }
    if (c1_ok != 0) // 如果备份 1 元数据有效
    {
        *meta = c1; // 取备份 1 元数据（副本 0 无效，下次 save 会修复它）
        return 0;   // 已读到合法备份
    }

    /* 两份均无效：使用默认值。通常发生在全新片或已损坏的情况。 */
    LOG_INFO("BOOT", "EEPROM meta empty/invalid, use defaults (CM7/CM4 active=A)");
    return 1; // meta 中是默认值，调用方可据此首次落盘
}

/**
 * @brief  保存元数据：seq 自增并重算 CRC，轮换写入较旧或无效的那份备份
 * @param  meta 待保存的元数据
 * @retval 0 成功，-1 失败（未就绪/结构超长/写入失败）
 * 
 * @note 该函数会强制刷新头字段，seq 自增并重算 CRC，轮换写入较旧或无效的那份备份。
 *       两份均无效时，会使用默认值（CM7/CM4 active=A）写入备份 0。
 *       只有当备份 1 无效时，才会将当前 meta 写入备份 1。
 *       默认值的真正来源是 boot_eeprom_load() 在返回前就填好的。
 */
int boot_eeprom_save(boot_eeprom_meta_t *meta)
{
    boot_eeprom_meta_t c0; // EEPROM 备份 0 地址元数据
    boot_eeprom_meta_t c1; // EEPROM 备份 1 地址元数据
    uint16_t target;       // 本次实际写入的备份地址
    int c0_ok = 0;         // EEPROM 备份 0 地址有效标志
    int c1_ok = 0;         // EEPROM 备份 1 地址有效标志

    /* 空指针或 EEPROM 未初始化 */
    if ((meta == NULL) || (s_ee_ready == 0U))
    {
        return -1; // 拒绝写入
    }

    /* 结构体超出单份备份区容量 */
    if (sizeof(boot_eeprom_meta_t) > BOOT_EE_COPY_SIZE)
    {
        LOG_ERROR("BOOT", "EEPROM meta too large (%u > %u)",
                  (unsigned int)sizeof(boot_eeprom_meta_t),
                  (unsigned int)BOOT_EE_COPY_SIZE);
        return -1; // 布局超限，无法落盘
    }

    /* 强制刷新头字段，seq 自增并重算 CRC（seq 用于双备份新旧裁决） */
    meta->magic = BOOT_EE_MAGIC;                         // 重写魔数，防止上层传入脏数据
    meta->struct_ver = BOOT_EE_STRUCT_VER;               // 重写结构版本号
    meta->length = (uint16_t)sizeof(boot_eeprom_meta_t); // 重写结构长度
    meta->seq++;                                         // 序列号自增，保证本份比旧副本更新
    meta->crc32 = boot_ee_crc32(meta);                   // 按最新内容重算校验值

    /* 首次上电时，双份均无效，直接写入默认值 */
    c0_ok = boot_ee_read_copy_as_v2(BOOT_EE_COPY0_ADDR, &c0); // 读回 EEPROM 备份 0 地址的元数据，返回有效性标志
    c1_ok = boot_ee_read_copy_as_v2(BOOT_EE_COPY1_ADDR, &c1); // 读回 EEPROM 备份 1 地址的元数据，返回有效性标志

    if ((c0_ok != 0) && (c1_ok != 0)) // 双份都有效
    {
        target = (c0.seq <= c1.seq) ? BOOT_EE_COPY0_ADDR : BOOT_EE_COPY1_ADDR; // 写入 seq 更旧的那份，保留最新副本
    }
    /* 当只有备份 1 无效时，才会将当前 meta 写入备份 1 */
    else if (c0_ok == 0) // 副本 0 无效（或已损坏）
    {
        target = BOOT_EE_COPY0_ADDR; // 优先写入副本 0
    }
    else if (c1_ok == 0) // 副本 1 无效（或已损坏）
    {
        target = BOOT_EE_COPY1_ADDR; // 写入副本 1
    }

    /* 两份均无效：使用默认值。默认值的真正来源是 boot_eeprom_load() 在返回前就填好的。 */
    /* 将元数据写入 EEPROM 目标地址 */
    if (BL24C16F_Write(&s_ee_dev, target, (const uint8_t *)meta, sizeof(*meta)) != HAL_OK) // I2C 写入失败
    {
        LOG_ERROR("BOOT", "EEPROM save failed @ 0x%04X", (unsigned int)target); // 打印失败地址
        return -1;                                                              // 写入失败（另一份副本仍在，启动不受影响）
    }

    return 0; // 写入成功
}

/**
 * @brief  选择 CM7 启动槽：pending（需置位且有效）→ active → Slot A → Slot B
 * @param  meta 元数据，可为 NULL
 * @retval 槽索引，meta 为 NULL 时返回 Slot A
 */
uint8_t boot_eeprom_select_boot_slot(const boot_eeprom_meta_t *meta)
{
    if (meta == NULL) // 无元数据可用
    {
        return CM7_APP_SLOT_A_INDEX; // 缺省返回 A 槽
    }

    /* pending 需同时满足：标志置位、索引在边界内、槽内镜像校验有效 */
    if (((meta->flags & BOOT_EE_FLAG_PENDING) != 0U) &&
        (meta->pending_slot < CM7_APP_SLOT_COUNT) &&
        (boot_slot_is_valid(meta->pending_slot) != 0))
    {
        return meta->pending_slot; // OTA 待启动槽优先
    }

    if ((meta->active_slot < CM7_APP_SLOT_COUNT) &&   // active 索引在边界内
        (boot_slot_is_valid(meta->active_slot) != 0)) // 且槽内镜像有效
    {
        return meta->active_slot; // 启动当前激活槽
    }

    /* active 也不可用：按 Slot A → Slot B 兜底 */
    if (boot_slot_is_valid(CM7_APP_SLOT_A_INDEX) != 0) // A 槽镜像有效
    {
        return CM7_APP_SLOT_A_INDEX; // 兜底启动 A 槽
    }

    if (boot_slot_is_valid(CM7_APP_SLOT_B_INDEX) != 0) // B 槽镜像有效
    {
        return CM7_APP_SLOT_B_INDEX; // 兜底启动 B 槽
    }

    return CM7_APP_SLOT_A_INDEX; // 无任何有效镜像：仍返回 A，由上层校验失败后留在 Bootloader
}

/**
 * @brief  CM7 试启动处理：从 pending 槽启动前递减 boot_trial，用尽则清 pending
 *         回退 active 并落盘
 * @param  meta 元数据，可为 NULL
 * @retval 最终应启动的槽索引
 * 
 * @note  仅当本次确实启动 pending 槽时才消耗试启动额度。
 *        其他情况直接返回 active 槽。
 *        落盘失败不影响本次启动槽选择。
 *        试启动额度用尽后，会自动取消 pending，回退 active 槽。
 *        回退 active 槽时，会按 A → B 兜底选择。
 *        回退后，会将试启动额度重置为初始值。
 *        兜底返回 A 槽。
 */
uint8_t boot_eeprom_prepare_boot(boot_eeprom_meta_t *meta)
{
    uint8_t slot; // 本次最终选定的槽索引

    if (meta == NULL) // 无元数据
    {
        return CM7_APP_SLOT_A_INDEX; // 缺省返回 A 槽
    }

    slot = boot_eeprom_select_boot_slot(meta); // 按 pending → active → A → B 选择

    /* 仅当本次确实启动 pending 槽时才消耗试启动额度 */
    if (((meta->flags & BOOT_EE_FLAG_PENDING) != 0U) &&
        (slot == meta->pending_slot))
    {
        if (meta->boot_trial > 0U) // 额度尚有剩余
        {
            meta->boot_trial--; // 扣减一次试启动机会
        }

        if (meta->boot_trial == 0U) // 额度耗尽
        {
            /* 额度用尽：取消 pending，回退 active */
            LOG_INFO("BOOT", "CM7 pending trial exhausted, fallback active=%u",
                     (unsigned int)meta->active_slot);
            meta->flags &= (uint8_t)~BOOT_EE_FLAG_PENDING; // 清除 PENDING 标志
            meta->pending_slot = meta->active_slot;        // pending 指回 active
            slot = meta->active_slot;                      // 本次改启动 active 槽
            /* active 槽镜像也无效时按回退链重新选择 */
            if (boot_slot_is_valid(slot) == 0) // 回退目标镜像无效
            {
                slot = boot_eeprom_select_boot_slot(meta); // 重新走一遍选择链兜底
            }
        }

        /* 落盘失败不影响本次启动槽选择 */
        (void)boot_eeprom_save(meta); // 试启动计数或回退结果写回 EEPROM
    }

    return slot; // 返回最终应启动的槽
}

/**
 * @brief  OTA 成功后由 APP 调用：pending 槽转正为 active，清 trial/pending 标志并保存
 * @param  meta 元数据，可为 NULL（无 pending 时直接返回）
 */
void boot_eeprom_commit_pending(boot_eeprom_meta_t *meta)
{
    if ((meta == NULL) || ((meta->flags & BOOT_EE_FLAG_PENDING) == 0U)) // 空指针或无 pending 待提交
    {
        return; // 无需提交，直接返回
    }

    meta->active_slot = meta->pending_slot;        // pending 槽转正为 active
    meta->boot_trial = 0U;                         // 清零试启动计数
    meta->flags &= (uint8_t)~BOOT_EE_FLAG_PENDING; // 清除 PENDING 标志
    (void)boot_eeprom_save(meta);                  // 提交结果落盘（失败仅打印日志）
}

/**
 * @brief  标记 CM7 pending 槽（OTA 写完镜像后由 APP 调用）：记录镜像信息，
 *         置试启动次数与 pending 标志后保存
 * @param  meta       元数据
 * @param  slot_index 待启动槽索引
 * @param  image_size 镜像大小
 * @param  image_crc32 镜像 CRC32
 * @param  version    镜像版本号
 */
void boot_eeprom_set_pending(boot_eeprom_meta_t *meta, uint8_t slot_index,
                             uint32_t image_size, uint32_t image_crc32,
                             uint32_t version)
{
    if ((meta == NULL) || (slot_index >= CM7_APP_SLOT_COUNT)) // 空指针或槽号越界
    {
        return; // 参数非法，拒绝标记
    }

    meta->pending_slot = slot_index;         // 记录 OTA 目标槽
    meta->app_size[slot_index] = image_size; // 记录新镜像大小
    meta->app_crc[slot_index] = image_crc32; // 记录新镜像 CRC32
    meta->app_version[slot_index] = version; // 记录新镜像版本号
    /* 预留 BOOT_EE_TRIAL_MAX 次试启动，期间未 commit 则自动回退 active */
    meta->boot_trial = BOOT_EE_TRIAL_MAX; // 预置 3 次试启动额度
    meta->flags |= BOOT_EE_FLAG_PENDING;  // 置位 PENDING，下次开机优先启动该槽
    (void)boot_eeprom_save(meta);         // 标记结果落盘
}

/**
 * @brief  选择 CM4 启动槽：pending（需置位且有效）→ active → Slot A → Slot B
 * @param  meta 元数据，可为 NULL
 * @retval 槽索引，meta 为 NULL 时返回 Slot A
 */
uint8_t boot_eeprom_select_cm4_boot_slot(const boot_eeprom_meta_t *meta)
{
    if (meta == NULL) // 无元数据可用
    {
        return CM4_APP_SLOT_A_INDEX; // 缺省返回 CM4 A 槽
    }

    /* pending 需同时满足：标志置位、索引在边界内、槽内镜像校验有效 */
    if (((meta->cm4_flags & BOOT_EE_FLAG_CM4_PENDING) != 0U) &&
        ((meta->cm4_pending_slot == CM4_APP_SLOT_A_INDEX) ||
         (meta->cm4_pending_slot == CM4_APP_SLOT_B_INDEX)) &&
        (boot_slot_is_valid(meta->cm4_pending_slot) != 0))
    {
        return meta->cm4_pending_slot; // CM4 OTA 待启动槽优先
    }

    if (((meta->cm4_active_slot == CM4_APP_SLOT_A_INDEX) || // active 索引合法
         (meta->cm4_active_slot == CM4_APP_SLOT_B_INDEX)) &&
        (boot_slot_is_valid(meta->cm4_active_slot) != 0)) // 且槽内镜像有效
    {
        return meta->cm4_active_slot; // 启动 CM4 当前激活槽
    }

    /* active 也不可用：按 Slot A → Slot B 兜底 */
    if (boot_slot_is_valid(CM4_APP_SLOT_A_INDEX) != 0) // A 槽镜像有效
    {
        return CM4_APP_SLOT_A_INDEX; // 兜底启动 CM4 A 槽
    }

    if (boot_slot_is_valid(CM4_APP_SLOT_B_INDEX) != 0) // B 槽镜像有效
    {
        return CM4_APP_SLOT_B_INDEX; // 兜底启动 CM4 B 槽
    }

    return CM4_APP_SLOT_A_INDEX; // 无有效镜像：仍返回 A，由 CM4 侧自行处理
}

/**
 * @brief  CM4 试启动处理：从 pending 槽启动前递减 cm4_boot_trial，用尽则清
 *         pending 回退 active 并落盘
 * @param  meta 元数据，可为 NULL
 * @retval 最终应启动的 CM4 槽索引
 */
uint8_t boot_eeprom_prepare_cm4_boot(boot_eeprom_meta_t *meta)
{
    uint8_t slot; // 本次最终选定的 CM4 槽索引

    if (meta == NULL) // 无元数据
    {
        return CM4_APP_SLOT_A_INDEX; // 缺省返回 CM4 A 槽
    }

    slot = boot_eeprom_select_cm4_boot_slot(meta); // 按 pending → active → A → B 选择

    /* 仅当本次确实启动 pending 槽时才消耗试启动额度 */
    if (((meta->cm4_flags & BOOT_EE_FLAG_CM4_PENDING) != 0U) &&
        (slot == meta->cm4_pending_slot))
    {
        if (meta->cm4_boot_trial > 0U) // 额度尚有剩余
        {
            meta->cm4_boot_trial--; // 扣减一次试启动机会
        }

        if (meta->cm4_boot_trial == 0U) // 额度耗尽
        {
            /* 额度用尽：取消 pending，回退 active */
            LOG_INFO("BOOT", "CM4 pending trial exhausted, fallback active=%u",
                     (unsigned int)meta->cm4_active_slot);
            meta->cm4_flags &= (uint8_t)~BOOT_EE_FLAG_CM4_PENDING; // 清除 CM4 PENDING 标志
            meta->cm4_pending_slot = meta->cm4_active_slot;        // pending 指回 active
            slot = meta->cm4_active_slot;                          // 本次改启动 active 槽
            /* active 槽镜像也无效时按回退链重新选择 */
            if (boot_slot_is_valid(slot) == 0) // 回退目标镜像无效
            {
                slot = boot_eeprom_select_cm4_boot_slot(meta); // 重新走一遍选择链兜底
            }
        }

        /* 落盘失败不影响本次启动槽选择 */
        (void)boot_eeprom_save(meta); // 试启动计数或回退结果写回 EEPROM
    }

    return slot; // 返回最终应启动的 CM4 槽
}

/**
 * @brief  CM4 OTA 成功后：pending 槽转正为 active，清 trial/pending 标志并保存
 * @param  meta 元数据，可为 NULL（无 pending 时直接返回）
 */
void boot_eeprom_commit_cm4_pending(boot_eeprom_meta_t *meta)
{
    if ((meta == NULL) || ((meta->cm4_flags & BOOT_EE_FLAG_CM4_PENDING) == 0U)) // 空指针或无 pending 待提交
    {
        return; // 无需提交，直接返回
    }

    meta->cm4_active_slot = meta->cm4_pending_slot;        // CM4 pending 槽转正为 active
    meta->cm4_boot_trial = 0U;                             // 清零 CM4 试启动计数
    meta->cm4_flags &= (uint8_t)~BOOT_EE_FLAG_CM4_PENDING; // 清除 CM4 PENDING 标志
    (void)boot_eeprom_save(meta);                          // 提交结果落盘（失败仅打印日志）
}

/**
 * @brief  标记 CM4 pending 槽：记录镜像信息，置试启动次数与 pending 标志后保存
 * @param  meta        元数据
 * @param  slot_index  待启动槽索引
 * @param  image_size  镜像大小
 * @param  image_crc32 镜像 CRC32
 * @param  version     镜像版本号
 */
void boot_eeprom_set_cm4_pending(boot_eeprom_meta_t *meta, uint8_t slot_index,
                                 uint32_t image_size, uint32_t image_crc32,
                                 uint32_t version)
{
    if ((meta == NULL) ||                                                               // 空指针
        ((slot_index != CM4_APP_SLOT_A_INDEX) && (slot_index != CM4_APP_SLOT_B_INDEX))) // 槽号非 A 非 B
    {
        return; // 参数非法，拒绝标记
    }

    meta->cm4_pending_slot = slot_index;                                // 记录 CM4 OTA 目标槽
    meta->cm4_app_size[slot_index - CM4_APP_SLOT_A_INDEX] = image_size; // CM4 数组下标需减去 A 槽索引
    meta->cm4_app_crc[slot_index - CM4_APP_SLOT_A_INDEX] = image_crc32; // 记录新镜像 CRC32
    meta->cm4_app_version[slot_index - CM4_APP_SLOT_A_INDEX] = version; // 记录新镜像版本号
    /* 预留 BOOT_EE_TRIAL_MAX 次试启动，期间未 commit 则自动回退 active */
    meta->cm4_boot_trial = BOOT_EE_TRIAL_MAX;    // 预置 3 次试启动额度
    meta->cm4_flags |= BOOT_EE_FLAG_CM4_PENDING; // 置位 CM4 PENDING
    (void)boot_eeprom_save(meta);                // 标记结果落盘
}
