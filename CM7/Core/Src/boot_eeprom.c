/**
 * @file    boot_eeprom.c
 * @brief   EEPROM 双备份启动元数据实现（CM7 + CM4）
 */
#include "boot_eeprom.h"
#include <string.h>
#include "bl24c16f.h"
#include "i2c.h"
#include "mylog.h"

#define BOOT_EE_TRIAL_MAX  (3U)

/** struct_ver=1 布局（升级前），仅用于迁移读取 */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t struct_ver;
    uint16_t length;
    uint8_t active_slot;
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

static uint32_t boot_ee_crc32_bytes(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFU;

    for (uint32_t i = 0U; i < length; i++) {
        crc ^= data[i];
        for (uint32_t bit = 0U; bit < 8U; bit++) {
            if ((crc & 1U) != 0U) {
                crc = (crc >> 1) ^ 0xEDB88320U;
            } else {
                crc >>= 1;
            }
        }
    }

    return ~crc;
}

static uint32_t boot_ee_crc32(const boot_eeprom_meta_t *meta)
{
    return boot_ee_crc32_bytes((const uint8_t *)meta,
                               sizeof(boot_eeprom_meta_t) - sizeof(uint32_t));
}

static uint32_t boot_ee_crc32_v1(const boot_eeprom_meta_v1_t *meta)
{
    return boot_ee_crc32_bytes((const uint8_t *)meta,
                               sizeof(boot_eeprom_meta_v1_t) - sizeof(uint32_t));
}

static void boot_ee_init_cm4_defaults(boot_eeprom_meta_t *meta)
{
    meta->cm4_active_slot = CM4_APP_SLOT_A_INDEX;
    meta->cm4_pending_slot = CM4_APP_SLOT_A_INDEX;
    meta->cm4_boot_trial = 0U;
    meta->cm4_flags = 0U;
    memset(meta->cm4_app_version, 0, sizeof(meta->cm4_app_version));
    memset(meta->cm4_app_size, 0, sizeof(meta->cm4_app_size));
    memset(meta->cm4_app_crc, 0, sizeof(meta->cm4_app_crc));
}

static void boot_ee_set_defaults(boot_eeprom_meta_t *meta)
{
    memset(meta, 0, sizeof(*meta));
    meta->magic = BOOT_EE_MAGIC;
    meta->struct_ver = BOOT_EE_STRUCT_VER;
    meta->length = (uint16_t)sizeof(boot_eeprom_meta_t);
    meta->active_slot = CM7_APP_SLOT_A_INDEX;
    meta->pending_slot = CM7_APP_SLOT_A_INDEX;
    meta->boot_trial = 0U;
    meta->flags = 0U;
    boot_ee_init_cm4_defaults(meta);
    meta->seq = 1U;
    meta->crc32 = boot_ee_crc32(meta);
}

static void boot_ee_upgrade_v1_to_v2(const boot_eeprom_meta_v1_t *v1,
                                    boot_eeprom_meta_t *out)
{
    memset(out, 0, sizeof(*out));
    out->magic = BOOT_EE_MAGIC;
    out->struct_ver = BOOT_EE_STRUCT_VER;
    out->length = (uint16_t)sizeof(boot_eeprom_meta_t);
    out->active_slot = v1->active_slot;
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

static int boot_ee_copy_valid_v2(const boot_eeprom_meta_t *meta)
{
    if (meta->magic != BOOT_EE_MAGIC) {
        return 0;
    }
    if (meta->struct_ver != BOOT_EE_STRUCT_VER) {
        return 0;
    }
    if (meta->length != (uint16_t)sizeof(boot_eeprom_meta_t)) {
        return 0;
    }
    if (meta->active_slot >= CM7_APP_SLOT_COUNT) {
        return 0;
    }
    if (meta->pending_slot >= CM7_APP_SLOT_COUNT) {
        return 0;
    }
    if (meta->cm4_active_slot >= CM4_APP_SLOT_COUNT) {
        return 0;
    }
    if (meta->cm4_pending_slot >= CM4_APP_SLOT_COUNT) {
        return 0;
    }
    if (meta->crc32 != boot_ee_crc32(meta)) {
        return 0;
    }
    return 1;
}

static int boot_ee_copy_valid_v1(const boot_eeprom_meta_v1_t *meta)
{
    if (meta->magic != BOOT_EE_MAGIC) {
        return 0;
    }
    if (meta->struct_ver != BOOT_EE_STRUCT_VER_V1) {
        return 0;
    }
    if (meta->length != (uint16_t)sizeof(boot_eeprom_meta_v1_t)) {
        return 0;
    }
    if (meta->active_slot >= CM7_APP_SLOT_COUNT) {
        return 0;
    }
    if (meta->pending_slot >= CM7_APP_SLOT_COUNT) {
        return 0;
    }
    if (meta->crc32 != boot_ee_crc32_v1(meta)) {
        return 0;
    }
    return 1;
}

static int boot_ee_read_raw(uint16_t addr, uint8_t *buf, uint16_t len)
{
    if (BL24C16F_Read(&s_ee_dev, addr, buf, len) != HAL_OK) {
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

    if (sizeof(boot_eeprom_meta_t) > BOOT_EE_COPY_SIZE) {
        return 0;
    }

    memset(raw, 0, sizeof(raw));
    if (boot_ee_read_raw(addr, raw, (uint16_t)sizeof(raw)) != 0) {
        return 0;
    }

    memcpy(&v2, raw, sizeof(v2));
    if (boot_ee_copy_valid_v2(&v2) != 0) {
        *out = v2;
        return 1;
    }

    memcpy(&v1, raw, sizeof(v1));
    if (boot_ee_copy_valid_v1(&v1) != 0) {
        boot_ee_upgrade_v1_to_v2(&v1, out);
        return 1;
    }

    return 0;
}

int boot_eeprom_init(void)
{
    s_ee_ready = 0U;
    BL24C16F_Init(&s_ee_dev, &hi2c4);

    if (BL24C16F_IsReady(&s_ee_dev) != HAL_OK) {
        LOG_ERROR("APP", "EEPROM not ready");
        return -1;
    }

    s_ee_ready = 1U;
    LOG_INFO("APP", "EEPROM ready (dual copy @ 0x%04X / 0x%04X, ver=%u)",
           (unsigned int)BOOT_EE_COPY0_ADDR,
           (unsigned int)BOOT_EE_COPY1_ADDR,
           (unsigned int)BOOT_EE_STRUCT_VER);
    return 0;
}

int boot_eeprom_load(boot_eeprom_meta_t *meta)
{
    boot_eeprom_meta_t c0;
    boot_eeprom_meta_t c1;
    int c0_ok = 0;
    int c1_ok = 0;

    if (meta == NULL) {
        return -1;
    }

    boot_ee_set_defaults(meta);

    if (s_ee_ready == 0U) {
        return 1;
    }

    c0_ok = boot_ee_read_copy_as_v2(BOOT_EE_COPY0_ADDR, &c0);
    c1_ok = boot_ee_read_copy_as_v2(BOOT_EE_COPY1_ADDR, &c1);

    if ((c0_ok != 0) && (c1_ok != 0)) {
        *meta = (c0.seq >= c1.seq) ? c0 : c1;
        return 0;
    }
    if (c0_ok != 0) {
        *meta = c0;
        return 0;
    }
    if (c1_ok != 0) {
        *meta = c1;
        return 0;
    }

    LOG_INFO("APP", "EEPROM meta empty/invalid, use defaults (CM7/CM4 active=A)");
    return 1;
}

int boot_eeprom_save(boot_eeprom_meta_t *meta)
{
    boot_eeprom_meta_t c0;
    boot_eeprom_meta_t c1;
    uint16_t target;
    int c0_ok = 0;
    int c1_ok = 0;

    if ((meta == NULL) || (s_ee_ready == 0U)) {
        return -1;
    }

    if (sizeof(boot_eeprom_meta_t) > BOOT_EE_COPY_SIZE) {
        LOG_ERROR("APP", "EEPROM meta too large (%u > %u)",
               (unsigned int)sizeof(boot_eeprom_meta_t),
               (unsigned int)BOOT_EE_COPY_SIZE);
        return -1;
    }

    meta->magic = BOOT_EE_MAGIC;
    meta->struct_ver = BOOT_EE_STRUCT_VER;
    meta->length = (uint16_t)sizeof(boot_eeprom_meta_t);
    meta->seq++;
    meta->crc32 = boot_ee_crc32(meta);

    /* 轮换写入：优先覆盖 seq 更旧或无效的那一份 */
    c0_ok = boot_ee_read_copy_as_v2(BOOT_EE_COPY0_ADDR, &c0);
    c1_ok = boot_ee_read_copy_as_v2(BOOT_EE_COPY1_ADDR, &c1);

    if ((c0_ok != 0) && (c1_ok != 0)) {
        target = (c0.seq <= c1.seq) ? BOOT_EE_COPY0_ADDR : BOOT_EE_COPY1_ADDR;
    } else if (c0_ok == 0) {
        target = BOOT_EE_COPY0_ADDR;
    } else {
        target = BOOT_EE_COPY1_ADDR;
    }

    if (BL24C16F_Write(&s_ee_dev, target, (const uint8_t *)meta, sizeof(*meta)) != HAL_OK) {
        LOG_ERROR("APP", "EEPROM save failed @ 0x%04X", (unsigned int)target);
        return -1;
    }

    return 0;
}

uint8_t boot_eeprom_select_boot_slot(const boot_eeprom_meta_t *meta)
{
    if (meta == NULL) {
        return CM7_APP_SLOT_A_INDEX;
    }

    if (((meta->flags & BOOT_EE_FLAG_PENDING) != 0U) &&
        (meta->pending_slot < CM7_APP_SLOT_COUNT) &&
        (boot_slot_is_valid(meta->pending_slot) != 0)) {
        return meta->pending_slot;
    }

    if ((meta->active_slot < CM7_APP_SLOT_COUNT) &&
        (boot_slot_is_valid(meta->active_slot) != 0)) {
        return meta->active_slot;
    }

    if (boot_slot_is_valid(CM7_APP_SLOT_A_INDEX) != 0) {
        return CM7_APP_SLOT_A_INDEX;
    }

    if (boot_slot_is_valid(CM7_APP_SLOT_B_INDEX) != 0) {
        return CM7_APP_SLOT_B_INDEX;
    }

    return CM7_APP_SLOT_A_INDEX;
}

uint8_t boot_eeprom_prepare_boot(boot_eeprom_meta_t *meta)
{
    uint8_t slot;

    if (meta == NULL) {
        return CM7_APP_SLOT_A_INDEX;
    }

    slot = boot_eeprom_select_boot_slot(meta);

    if (((meta->flags & BOOT_EE_FLAG_PENDING) != 0U) &&
        (slot == meta->pending_slot)) {
        if (meta->boot_trial > 0U) {
            meta->boot_trial--;
        }

        if (meta->boot_trial == 0U) {
            LOG_INFO("APP", "CM7 pending trial exhausted, fallback active=%u",
                   (unsigned int)meta->active_slot);
            meta->flags &= (uint8_t)~BOOT_EE_FLAG_PENDING;
            meta->pending_slot = meta->active_slot;
            slot = meta->active_slot;
            if (boot_slot_is_valid(slot) == 0) {
                slot = boot_eeprom_select_boot_slot(meta);
            }
        }

        (void)boot_eeprom_save(meta);
    }

    return slot;
}

void boot_eeprom_commit_pending(boot_eeprom_meta_t *meta)
{
    if ((meta == NULL) || ((meta->flags & BOOT_EE_FLAG_PENDING) == 0U)) {
        return;
    }

    meta->active_slot = meta->pending_slot;
    meta->boot_trial = 0U;
    meta->flags &= (uint8_t)~BOOT_EE_FLAG_PENDING;
    (void)boot_eeprom_save(meta);
}

void boot_eeprom_set_pending(boot_eeprom_meta_t *meta, uint8_t slot_index,
                             uint32_t image_size, uint32_t image_crc32,
                             uint32_t version)
{
    if ((meta == NULL) || (slot_index >= CM7_APP_SLOT_COUNT)) {
        return;
    }

    meta->pending_slot = slot_index;
    meta->app_size[slot_index] = image_size;
    meta->app_crc[slot_index] = image_crc32;
    meta->app_version[slot_index] = version;
    meta->boot_trial = BOOT_EE_TRIAL_MAX;
    meta->flags |= BOOT_EE_FLAG_PENDING;
    (void)boot_eeprom_save(meta);
}

uint8_t boot_eeprom_select_cm4_boot_slot(const boot_eeprom_meta_t *meta)
{
    if (meta == NULL) {
        return CM4_APP_SLOT_A_INDEX;
    }

    if (((meta->cm4_flags & BOOT_EE_FLAG_CM4_PENDING) != 0U) &&
        (meta->cm4_pending_slot < CM4_APP_SLOT_COUNT) &&
        (boot_slot_is_valid(meta->cm4_pending_slot) != 0)) {
        return meta->cm4_pending_slot;
    }

    if ((meta->cm4_active_slot < CM4_APP_SLOT_COUNT) &&
        (boot_slot_is_valid(meta->cm4_active_slot) != 0)) {
        return meta->cm4_active_slot;
    }

    if (boot_slot_is_valid(CM4_APP_SLOT_A_INDEX) != 0) {
        return CM4_APP_SLOT_A_INDEX;
    }

    if (boot_slot_is_valid(CM4_APP_SLOT_B_INDEX) != 0) {
        return CM4_APP_SLOT_B_INDEX;
    }

    return CM4_APP_SLOT_A_INDEX;
}

uint8_t boot_eeprom_prepare_cm4_boot(boot_eeprom_meta_t *meta)
{
    uint8_t slot;

    if (meta == NULL) {
        return CM4_APP_SLOT_A_INDEX;
    }

    slot = boot_eeprom_select_cm4_boot_slot(meta);

    if (((meta->cm4_flags & BOOT_EE_FLAG_CM4_PENDING) != 0U) &&
        (slot == meta->cm4_pending_slot)) {
        if (meta->cm4_boot_trial > 0U) {
            meta->cm4_boot_trial--;
        }

        if (meta->cm4_boot_trial == 0U) {
            LOG_INFO("APP", "CM4 pending trial exhausted, fallback active=%u",
                   (unsigned int)meta->cm4_active_slot);
            meta->cm4_flags &= (uint8_t)~BOOT_EE_FLAG_CM4_PENDING;
            meta->cm4_pending_slot = meta->cm4_active_slot;
            slot = meta->cm4_active_slot;
            if (boot_slot_is_valid(slot) == 0) {
                slot = boot_eeprom_select_cm4_boot_slot(meta);
            }
        }

        (void)boot_eeprom_save(meta);
    }

    return slot;
}

void boot_eeprom_commit_cm4_pending(boot_eeprom_meta_t *meta)
{
    if ((meta == NULL) || ((meta->cm4_flags & BOOT_EE_FLAG_CM4_PENDING) == 0U)) {
        return;
    }

    meta->cm4_active_slot = meta->cm4_pending_slot;
    meta->cm4_boot_trial = 0U;
    meta->cm4_flags &= (uint8_t)~BOOT_EE_FLAG_CM4_PENDING;
    (void)boot_eeprom_save(meta);
}

void boot_eeprom_set_cm4_pending(boot_eeprom_meta_t *meta, uint8_t slot_index,
                                 uint32_t image_size, uint32_t image_crc32,
                                 uint32_t version)
{
    if ((meta == NULL) || (slot_index >= CM4_APP_SLOT_COUNT)) {
        return;
    }

    meta->cm4_pending_slot = slot_index;
    meta->cm4_app_size[slot_index] = image_size;
    meta->cm4_app_crc[slot_index] = image_crc32;
    meta->cm4_app_version[slot_index] = version;
    meta->cm4_boot_trial = BOOT_EE_TRIAL_MAX;
    meta->cm4_flags |= BOOT_EE_FLAG_CM4_PENDING;
    (void)boot_eeprom_save(meta);
}
