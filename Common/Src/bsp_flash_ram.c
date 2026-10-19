/**
 * @file    bsp_flash_ram.c
 * @brief   在可执行 RAM (.RamFunc) 中执行的 Bank1/Bank2 Flash 擦写实现
 *
 * 擦/写期间喂狗；program_buffer 按地址自动选择 Bank1 或 Bank2 控制器。
 */
#include "bsp_flash_ram.h"

#include "stm32h7xx_hal.h"

extern volatile uint32_t uwTick;

#define BSP_FLASH_RAM_TIMEOUT_MS    (60000U)
#define BSP_FLASH_RAM_WORD_BYTES    (FLASH_NB_32BITWORD_IN_FLASHWORD * sizeof(uint32_t))

#if !defined(FLASH_BANK2_BASE)
#define FLASH_BANK2_BASE            (0x08100000UL)
#endif

static __RAM_FUNC uint32_t bsp_flash_ram_tick(void)
{
    return uwTick;
}

static __RAM_FUNC void bsp_flash_ram_wdg_kick(void)
{
#if defined(IWDG1)
    IWDG1->KR = 0x0000AAAAU;
#endif
}

/* -------------------- Bank1 -------------------- */

static __RAM_FUNC int bsp_flash_ram_wait_bank1(void)
{
    uint32_t tickstart = bsp_flash_ram_tick();
    uint32_t err;

    while ((FLASH->SR1 & FLASH_FLAG_QW_BANK1) != 0U) {
        bsp_flash_ram_wdg_kick();
        if ((bsp_flash_ram_tick() - tickstart) > BSP_FLASH_RAM_TIMEOUT_MS) {
            return -1;
        }
    }

    err = FLASH->SR1 & FLASH_FLAG_ALL_ERRORS_BANK1;
    if (err != 0U) {
        FLASH->CCR1 = err;
        return -1;
    }

    if ((FLASH->SR1 & FLASH_FLAG_EOP_BANK1) != 0U) {
        FLASH->CCR1 = FLASH_FLAG_EOP_BANK1;
    }

    return 0;
}

static __RAM_FUNC int bsp_flash_ram_unlock_bank1(void)
{
    if ((FLASH->CR1 & FLASH_CR_LOCK) != 0U) {
        FLASH->KEYR1 = FLASH_KEY1;
        FLASH->KEYR1 = FLASH_KEY2;
        if ((FLASH->CR1 & FLASH_CR_LOCK) != 0U) {
            return -1;
        }
    }

    return 0;
}

static __RAM_FUNC void bsp_flash_ram_lock_bank1(void)
{
    SET_BIT(FLASH->CR1, FLASH_CR_LOCK);
}

static __RAM_FUNC int bsp_flash_ram_erase_sector_bank1(uint32_t sector)
{
    if (bsp_flash_ram_wait_bank1() != 0) {
        return -1;
    }

#if defined(FLASH_CR_PSIZE)
    FLASH->CR1 &= ~(FLASH_CR_PSIZE | FLASH_CR_SNB);
    FLASH->CR1 |= (FLASH_CR_SER | FLASH_VOLTAGE_RANGE_3 |
                   (sector << FLASH_CR_SNB_Pos) | FLASH_CR_START);
#else
    FLASH->CR1 &= ~FLASH_CR_SNB;
    FLASH->CR1 |= (FLASH_CR_SER | (sector << FLASH_CR_SNB_Pos) | FLASH_CR_START);
#endif

    if (bsp_flash_ram_wait_bank1() != 0) {
        FLASH->CR1 &= ~(FLASH_CR_SER | FLASH_CR_SNB);
        return -1;
    }

    FLASH->CR1 &= ~(FLASH_CR_SER | FLASH_CR_SNB);
    return 0;
}

static __RAM_FUNC int bsp_flash_ram_program_word_bank1(uint32_t address, const uint32_t *data)
{
    uint32_t i;

    if (bsp_flash_ram_wait_bank1() != 0) {
        return -1;
    }

    SET_BIT(FLASH->CR1, FLASH_CR_PG);
    __ISB();
    __DSB();

    for (i = 0U; i < FLASH_NB_32BITWORD_IN_FLASHWORD; i++) {
        *(__IO uint32_t *)(address + (i * 4U)) = data[i];
    }

    __ISB();
    __DSB();

    if (bsp_flash_ram_wait_bank1() != 0) {
        CLEAR_BIT(FLASH->CR1, FLASH_CR_PG);
        return -1;
    }

    CLEAR_BIT(FLASH->CR1, FLASH_CR_PG);
    return 0;
}

/* -------------------- Bank2（CM4 Stub/APP，寄存器 CR2/SR2/KEYR2） -------------------- */

/** @brief 等待 Bank2 排队位清除并处理错误/EOP */
static __RAM_FUNC int bsp_flash_ram_wait_bank2(void)
{
    uint32_t tickstart = bsp_flash_ram_tick();
    uint32_t err;

    while ((FLASH->SR2 & FLASH_SR_QW) != 0U) {
        bsp_flash_ram_wdg_kick();
        if ((bsp_flash_ram_tick() - tickstart) > BSP_FLASH_RAM_TIMEOUT_MS) {
            return -1;
        }
    }

    err = FLASH->SR2 & (FLASH_FLAG_ALL_ERRORS_BANK2 & 0x7FFFFFFFU);
    if (err != 0U) {
        FLASH->CCR2 = err;
        return -1;
    }

    if ((FLASH->SR2 & FLASH_SR_EOP) != 0U) {
        FLASH->CCR2 = FLASH_SR_EOP;
    }

    return 0;
}

/** @brief 解锁 Bank2 Flash 控制寄存器 */
static __RAM_FUNC int bsp_flash_ram_unlock_bank2(void)
{
    if ((FLASH->CR2 & FLASH_CR_LOCK) != 0U) {
        FLASH->KEYR2 = FLASH_KEY1;
        FLASH->KEYR2 = FLASH_KEY2;
        if ((FLASH->CR2 & FLASH_CR_LOCK) != 0U) {
            return -1;
        }
    }

    return 0;
}

/** @brief 锁定 Bank2 Flash 控制寄存器 */
static __RAM_FUNC void bsp_flash_ram_lock_bank2(void)
{
    SET_BIT(FLASH->CR2, FLASH_CR_LOCK);
}

/**
 * @brief  擦除 Bank2 单个扇区
 * @param  sector  扇区号 0~7（OTA 业务层勿传 0）
 */
static __RAM_FUNC int bsp_flash_ram_erase_sector_bank2(uint32_t sector)
{
    if (bsp_flash_ram_wait_bank2() != 0) {
        return -1;
    }

#if defined(FLASH_CR_PSIZE)
    FLASH->CR2 &= ~(FLASH_CR_PSIZE | FLASH_CR_SNB);
    FLASH->CR2 |= (FLASH_CR_SER | FLASH_VOLTAGE_RANGE_3 |
                   (sector << FLASH_CR_SNB_Pos) | FLASH_CR_START);
#else
    FLASH->CR2 &= ~FLASH_CR_SNB;
    FLASH->CR2 |= (FLASH_CR_SER | (sector << FLASH_CR_SNB_Pos) | FLASH_CR_START);
#endif

    if (bsp_flash_ram_wait_bank2() != 0) {
        FLASH->CR2 &= ~(FLASH_CR_SER | FLASH_CR_SNB);
        return -1;
    }

    FLASH->CR2 &= ~(FLASH_CR_SER | FLASH_CR_SNB);
    return 0;
}

/** @brief 向 Bank2 写入一个 FlashWord（256 bit） */
static __RAM_FUNC int bsp_flash_ram_program_word_bank2(uint32_t address, const uint32_t *data)
{
    uint32_t i;

    if (bsp_flash_ram_wait_bank2() != 0) {
        return -1;
    }

    SET_BIT(FLASH->CR2, FLASH_CR_PG);
    __ISB();
    __DSB();

    for (i = 0U; i < FLASH_NB_32BITWORD_IN_FLASHWORD; i++) {
        *(__IO uint32_t *)(address + (i * 4U)) = data[i];
    }

    __ISB();
    __DSB();

    if (bsp_flash_ram_wait_bank2() != 0) {
        CLEAR_BIT(FLASH->CR2, FLASH_CR_PG);
        return -1;
    }

    CLEAR_BIT(FLASH->CR2, FLASH_CR_PG);
    return 0;
}

/* -------------------- Public -------------------- */

int __RAM_FUNC bsp_flash_ram_erase_sectors(uint32_t first_sector, uint32_t last_sector)
{
    uint32_t sector;

    if (first_sector > last_sector) {
        return -1;
    }

    if (bsp_flash_ram_unlock_bank1() != 0) {
        return -1;
    }

    FLASH->CCR1 = FLASH_FLAG_ALL_ERRORS_BANK1;

    for (sector = first_sector; sector <= last_sector; sector++) {
        bsp_flash_ram_wdg_kick();
        if (bsp_flash_ram_erase_sector_bank1(sector) != 0) {
            bsp_flash_ram_lock_bank1();
            return -1;
        }
    }

    bsp_flash_ram_lock_bank1();
    return 0;
}

/**
 * @brief  擦除 Bank2 指定扇区范围
 * @param  first_sector  起始扇区（须 >0，禁止擦 Stub S0）
 * @param  last_sector   结束扇区（含）
 * @retval 0 成功；-1 参数非法或擦除失败
 */
int __RAM_FUNC bsp_flash_ram_erase_bank2_sectors(uint32_t first_sector, uint32_t last_sector)
{
    uint32_t sector;

    if (first_sector > last_sector) {
        return -1;
    }

    /* 禁止擦除 Bank2 S0（CM4 Stub @ 0x08100000） */
    if (first_sector == 0U) {
        return -1;
    }

    if (bsp_flash_ram_unlock_bank2() != 0) {
        return -1;
    }

    FLASH->CCR2 = (FLASH_FLAG_ALL_ERRORS_BANK2 & 0x7FFFFFFFU);

    for (sector = first_sector; sector <= last_sector; sector++) {
        bsp_flash_ram_wdg_kick();
        if (bsp_flash_ram_erase_sector_bank2(sector) != 0) {
            bsp_flash_ram_lock_bank2();
            return -1;
        }
    }

    bsp_flash_ram_lock_bank2();
    return 0;
}

int __RAM_FUNC bsp_flash_ram_program_buffer(uint32_t address, const uint8_t *data, uint32_t length)
{
    uint32_t flashword[FLASH_NB_32BITWORD_IN_FLASHWORD];
    uint32_t offset = 0U;
    uint32_t chunk;
    uint32_t i;
    int is_bank2;

    if ((data == NULL) || (length == 0U)) {
        return -1;
    }

    if ((address % BSP_FLASH_RAM_WORD_BYTES) != 0U) {
        return -1;
    }

    is_bank2 = (address >= FLASH_BANK2_BASE) ? 1 : 0;

    if (is_bank2 != 0) {
        if (bsp_flash_ram_unlock_bank2() != 0) {
            return -1;
        }
    } else {
        if (bsp_flash_ram_unlock_bank1() != 0) {
            return -1;
        }
    }

    while (offset < length) {
        chunk = length - offset;
        if (chunk > BSP_FLASH_RAM_WORD_BYTES) {
            chunk = BSP_FLASH_RAM_WORD_BYTES;
        }

        for (i = 0U; i < FLASH_NB_32BITWORD_IN_FLASHWORD; i++) {
            flashword[i] = 0xFFFFFFFFU;
        }

        for (i = 0U; i < chunk; i++) {
            ((uint8_t *)flashword)[i] = data[offset + i];
        }

        bsp_flash_ram_wdg_kick();
        if (is_bank2 != 0) {
            if (bsp_flash_ram_program_word_bank2(address + offset, flashword) != 0) {
                bsp_flash_ram_lock_bank2();
                return -1;
            }
        } else {
            if (bsp_flash_ram_program_word_bank1(address + offset, flashword) != 0) {
                bsp_flash_ram_lock_bank1();
                return -1;
            }
        }

        offset += BSP_FLASH_RAM_WORD_BYTES;
    }

    if (is_bank2 != 0) {
        bsp_flash_ram_lock_bank2();
    } else {
        bsp_flash_ram_lock_bank1();
    }
    return 0;
}
