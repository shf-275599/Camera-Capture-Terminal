#include "boot_slot.h"
#include <stdint.h>
#include "stm32h7xx.h"
#include "mylog.h"

extern uint32_t g_pfnVectors[];

/**
 * @brief   检查 slot_index 是否有效
 * @param   slot_index  slot_index
 * @return  uint8_t     0：无效；1：有效
 */
int boot_slot_is_valid(uint8_t slot_index)
{
    uint32_t slot_base; // 扇区起始地址
    uint32_t slot_end;  // 扇区结束地址
    uint32_t stack;     // 扇区大小
    uint32_t reset;     // 重置向量地址

    /* 检查 slot_index 是否有效 */
    if (!boot_slot_index_is_valid(slot_index))
    {
        LOG_ERROR("BOOT", "slot_index=%u is not valid", (unsigned int)slot_index);
        return 0;
    }
    /* 获取 slot_base 地址 */
    slot_base = boot_slot_base_get(slot_index);
    /* 计算 slot_end 地址*/
    slot_end = boot_slot_end_get(slot_index);
    /* 计算 stack 大小 */
    stack = *(__IO uint32_t *)slot_base;
    reset = *(__IO uint32_t *)(slot_base + 4U);
    /* 检查 stack_region 是否有效 */
    if (!boot_slot_stack_region_is_valid(slot_index, stack))
    {
        LOG_ERROR("BOOT", "slot_index=%u stack_region is not valid", (unsigned int)slot_index);
        return 0;
    }
    /* 校验复位向量：地址必须在slot范围内，且最低位为1（Cortex-M Thumb模式） */
    if ((reset < slot_base) || (reset >= slot_end) || ((reset & 1U) == 0U))
    {
        LOG_ERROR("BOOT", "slot_index=%u reset is not valid", (unsigned int)slot_index);
        return 0;
    }

    return 1;
}

/**
 * @brief  根据地址获取对应的 slot_index
 * @param  address 地址
 * @retval slot_index
 */
uint8_t boot_get_slot_index_from_address(uint32_t address)
{
    /* 检查地址是否在 CM7_APP_SLOT_A 范围内 */
    if ((address >= CM7_APP_SLOT_A_BASE) &&
        (address < (CM7_APP_SLOT_A_BASE + CM7_APP_SLOT_A_SIZE))) {
        return CM7_APP_SLOT_A_INDEX;
    }
    /* 检查地址是否在 CM7_APP_SLOT_B 范围内 */
    else if ((address >= CM7_APP_SLOT_B_BASE) &&
        (address < (CM7_APP_SLOT_B_BASE + CM7_APP_SLOT_B_SIZE))) {
        return CM7_APP_SLOT_B_INDEX;
    }
    /* 检查地址是否在 CM4_APP_SLOT_A 范围内 */
    else if ((address >= CM4_APP_SLOT_A_BASE) &&
        (address < (CM4_APP_SLOT_A_BASE + CM4_APP_SLOT_A_SIZE))) {
        return CM4_APP_SLOT_A_INDEX;
    }
    /* 检查地址是否在 CM4_APP_SLOT_B 范围内 */
    else if ((address >= CM4_APP_SLOT_B_BASE) &&
        (address < (CM4_APP_SLOT_B_BASE + CM4_APP_SLOT_B_SIZE))) {
        return CM4_APP_SLOT_B_INDEX;
    } else {
        return 255U;
    }
}

/**
 * @brief   检查 slot_index 是否有效
 * @param   slot_index  slot_index
 * @return  uint8_t     0：无效；1：有效
 */
uint8_t boot_slot_index_is_valid(uint8_t slot_index)
{
    /* 检查 slot_index 是否有效 */
    if (slot_index != CM7_APP_SLOT_A_INDEX &&
        slot_index != CM7_APP_SLOT_B_INDEX &&
        slot_index != CM4_APP_SLOT_A_INDEX &&
        slot_index != CM4_APP_SLOT_B_INDEX)
    {
        return 0;
    }
    return 1;
}

/**
 * @brief   获取 slot_base 地址
 * @param   slot_index  slot_index
 * @return  uint32_t    slot_base 地址
 */
uint32_t boot_slot_base_get(uint8_t slot_index)
{
    /* 根据 slot_index 获取对应的 Flash 地址 */
    switch (slot_index)
    {
    case CM7_APP_SLOT_A_INDEX:
        return CM7_APP_SLOT_A_BASE;
        break;
    case CM7_APP_SLOT_B_INDEX:
        return CM7_APP_SLOT_B_BASE;
        break;
    case CM4_APP_SLOT_A_INDEX:
        return CM4_APP_SLOT_A_BASE;
        break;
    case CM4_APP_SLOT_B_INDEX:
        return CM4_APP_SLOT_B_BASE;
        break;
    default:
        return 0;
    }
}

/**
 * @brief   获取 slot_end 地址
 * @param   slot_index  slot_index
 * @return  uint32_t    slot_end 地址
 */
uint32_t boot_slot_end_get(uint8_t slot_index)
{
    switch (slot_index)
    {
    case CM7_APP_SLOT_A_INDEX:
        return CM7_APP_SLOT_A_BASE + CM7_APP_SLOT_A_SIZE;
        break;
    case CM7_APP_SLOT_B_INDEX:
        return CM7_APP_SLOT_B_BASE + CM7_APP_SLOT_B_SIZE;
        break;
    case CM4_APP_SLOT_A_INDEX:
        return CM4_APP_SLOT_A_BASE + CM4_APP_SLOT_A_SIZE;
        break;
    case CM4_APP_SLOT_B_INDEX:
        return CM4_APP_SLOT_B_BASE + CM4_APP_SLOT_B_SIZE;
        break;
    default:
        return 0;
    }
}

/**
 * @brief   检查 stack_region 是否有效
 * @param   slot_index  slot_index
 * @param   stack       stack 地址
 * @return  uint32_t    0：无效；1：有效
 */
uint32_t boot_slot_stack_region_is_valid(uint8_t slot_index, uint32_t stack)
{
    if (slot_index == CM7_APP_SLOT_A_INDEX ||
        slot_index == CM7_APP_SLOT_B_INDEX)
    {
        /* MSP：DTCM 0x200xxxxx / AXI SRAM(D1) 0x240xxxxx / SRAM1/2/3(D2) 0x300xxxxx */
        uint32_t stack_region = stack & 0xFF000000U; // 取地址最高字节，判定内存区域
        /* MSP栈顶必须落在 CM7 片内 RAM：DTCM / AXI SRAM(D1) / SRAM1/2/3(D2)，禁止 Flash / 外部 SDRAM */
        if ((stack_region != 0x20000000U) &&
            (stack_region != 0x24000000U) &&
            (stack_region != 0x30000000U))
        {
            return 0; // 栈不在这三块 RAM → slot 镜像非法，拒绝启动
        }
    }
    else if (slot_index == CM4_APP_SLOT_A_INDEX ||
             slot_index == CM4_APP_SLOT_B_INDEX)
    {
        uint32_t stack_region = stack & 0xFF000000U; // 取地址最高字节，判定内存区域
        /* MSP栈顶必须落在 CM4 片内 RAM：ITCM / DTCM / AXI SRAM，禁止 Flash / 外部 SDRAM */
        if ((stack_region != 0x10000000U) &&
            (stack_region != 0x30000000U) &&
            (stack_region != 0x20000000U))
        {
            return 0; // 栈不在这三块 RAM → slot 镜像非法，拒绝启动
        }
    }
    return 1; 
}

/**
 * @brief   检测当前运行的 slot_index
 * @return  uint8_t     slot_index
 */
uint8_t boot_detect_slot_running(void)
{
    return boot_get_slot_index_from_address((uint32_t)g_pfnVectors);
}
