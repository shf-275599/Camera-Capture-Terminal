#include "boot_jump.h"
#include "boot_config.h"
#include <stdbool.h>
#include <stdint.h>
#include <core_cm7.h>

bool Boot_IsAppValid(void)
{
    /* 向量表第0项：Flash 地址 CM7_APP_FLASH_BASE(0x08020000)，存储【初始MSP栈顶值】 */
    uint32_t stack = *(__IO uint32_t *)CM7_APP_FLASH_BASE;
    /* 向量表第1项：复位异常入口，存储 Reset_Handler函数指针，Cortex-M 要求最低位为 1(Thumb 标记) */
    uint32_t reset = *(__IO uint32_t *)(CM7_APP_FLASH_BASE + 4U);

    /**
     * 校验逻辑：
     * 1. stack 必须是合法 RAM 地址（在 H7 的 SRAM 范围内）
     * [0x08020000, 0x08100000)
     * 2. reset 最低位必须等于 1（Thumb模式）
     */

    /* 掩码提取内存块基地址，校验stack是否落在SRAM 0x20020000~0x2003FFFF，非法返回false */
    if ((stack & 0x2FFE0000UL) != 0x20020000UL)
    {
        return false;
    }

    /* reset 最低位必须等于 1（Thumb模式） */
    if ((reset < CM7_APP_FLASH_BASE) ||
        (reset >= 0x8100000U) ||
        ((reset & 1U) == 0U))
    {
        return false;
    }

    return true;
}

void Boot_JumpToAPP(void)
{
    
}
