#include "boot_slot.h"
#include <stdint.h>
#include <stdio.h>
#include "stm32h7xx.h"

static uint32_t g_pfnVectors[] = {0};

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
        return 0;
    }
    /* 校验复位向量：地址必须在slot范围内，且最低位为1（Cortex-M Thumb模式） */
    if ((reset < slot_base) || (reset >= slot_end) || ((reset & 1U) == 0U))
    {
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
        (address < (CM7_APP_SLOT_A_BASE + CM7_APP_SLOT_A_SIZE)))
    {
        return CM7_APP_SLOT_A_INDEX;
    }
    /* 检查地址是否在 CM7_APP_SLOT_B 范围内 */
    else if ((address >= CM7_APP_SLOT_B_BASE) &&
             (address < (CM7_APP_SLOT_B_BASE + CM7_APP_SLOT_B_SIZE)))
    {
        return CM7_APP_SLOT_B_INDEX;
    }
    else
    {
        return 255U;
    }
}

/**
 * @brief   根据地址获取 CM4 核内槽索引，槽外地址返回 255
 * @param   address 地址
 * @return  uint8_t     CM4 核内槽索引
 */
uint8_t cm4_boot_get_slot_index_from_address(uint32_t address)
{
    if ((address >= CM4_APP_SLOT_A_BASE) &&
        (address < (CM4_APP_SLOT_A_BASE + CM4_APP_SLOT_A_SIZE)))
    {
        return CM4_APP_SLOT_A_INDEX;
    }
    /* 检查地址是否在 CM4_APP_SLOT_B 范围内 */
    else if ((address >= CM4_APP_SLOT_B_BASE) &&
             (address < (CM4_APP_SLOT_B_BASE + CM4_APP_SLOT_B_SIZE)))
    {
        return CM4_APP_SLOT_B_INDEX;
    }
    else
    {
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
        slot_index != CM7_APP_SLOT_B_INDEX)
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
    default:
        return 0;
    }
}

/**
 * @brief   获取 CM4 核内槽的 Flash 基址，无效索引返回 0
 * @param   slot_index  slot_index
 * @return  uint32_t    CM4 核内槽的 Flash 基址
 */
uint32_t cm4_boot_slot_base_get(uint8_t slot_index)
{
    switch (slot_index)
    {
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
    default:
        return 0;
    }
}

/**
 * @brief   获取 CM4 核内槽的 Flash 结束地址，无效索引返回 0
 * @param   slot_index  slot_index
 * @return  uint32_t    CM4 核内槽的 Flash 结束地址
 */
uint32_t cm4_boot_slot_end_get(uint8_t slot_index)
{
    switch (slot_index)
    {
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
    return 1;
}

/**
 * @brief   校验 CM4 槽向量表：栈地址符合 CM4 规则，复位入口位于本槽且为 Thumb
 * @param   slot_index  slot_index
 * @return  int         0：无效；1：有效
 */
int cm4_boot_slot_is_valid(uint8_t slot_index)
{
    uint32_t slot_base;
    uint32_t slot_end;
    uint32_t stack;
    uint32_t reset;

    if (slot_index >= CM4_APP_SLOT_COUNT)
    {
        return 0;
    }

    slot_base = cm4_boot_slot_base_get(slot_index);
    slot_end = cm4_boot_slot_end_get(slot_index);
    stack = *(__IO uint32_t *)slot_base;
    reset = *(__IO uint32_t *)(slot_base + 4U);
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
    if ((reset < slot_base) || (reset >= slot_end) || ((reset & 1U) == 0U))
    {
        return 0;
    }
    return 1;
}

/**
 * @brief   检测当前运行的 slot_index
 * @return  uint8_t     slot_index
 */
uint8_t boot_get_running_slot(void)
{
    return boot_get_slot_index_from_address((uint32_t)g_pfnVectors);
}
