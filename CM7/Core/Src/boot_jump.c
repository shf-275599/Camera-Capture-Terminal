#include "boot_jump.h"
#include "boot_config.h"
#include "boot_slot.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "stm32h7xx.h"
#include "stm32h7xx_hal_rcc_ex.h"

/**
 * @brief 校验 APP 是否有效
 * @return true APP有效
 * @return false APP无效
 */
bool Boot_IsAppValid(void)
{
    /* 向量表第0项：Flash 地址 CM7_APP_FLASH_BASE(0x08020000)，存储【初始MSP栈顶值】 */
    uint32_t stack = *(__IO uint32_t *)CM7_APP_FLASH_BASE;
    /* 向量表第1项：复位异常入口，存储 Reset_Handler函数指针，Cortex-M 要求最低位为 1(Thumb 标记) */
    uint32_t reset = *(__IO uint32_t *)(CM7_APP_FLASH_BASE + 4U);

    /**
     * 校验逻辑：
     * 1. stack 必须是合法 RAM 地址（在 H7 的 SRAM 范围内）
     * [0x20020000, 0x2003FFFF]
     * 2. reset 最低位必须等于 1（Thumb 模式）
     */

    /* 掩码提取内存块基地址，校验 stack 是否落在 SRAM 0x20020000~0x2003FFFF，非法返回false */
    if ((stack & 0x2FFE0000UL) != 0x20020000UL)
    {
        printf ("stack: 0x%08lx\r\n", stack);
        return false;
    }

    /* reset 最低位必须等于 1（Thumb模式） */
    if ((reset < CM7_APP_FLASH_BASE) ||
        (reset >= 0x8100000U) ||
        ((reset & 1U) == 0U))
    {
        printf ("reset: 0x%08lx\r\n", reset);
        return false;
    }

    return true;
}

/**
 * @brief 跳转到 APP 复位入口 Reset_Handler
 * @note 该函数会直接跳转到 APP 复位入口 ，不返回
 */
void Boot_JumpToAPP(void)
{
    typedef void (*AppEntry_t)(void);                                       // 定义 APP 复位入口函数指针类型
    uint32_t app_stack = *(__IO uint32_t *)CM7_APP_FLASH_BASE;              // 读取 APP 向量表首地址：APP 栈顶地址
    uint32_t app_entry_reset = *(__IO uint32_t *)(CM7_APP_FLASH_BASE + 4U); // 向量表 +4，获取 APP 复位程序入口地址
    AppEntry_t jump = (AppEntry_t)app_entry_reset;                          // 将复位入口转为函数指针

    __disable_irq();    // 关闭全局中断，防止跳转过程中断触发
    SysTick->CTRL = 0U; // 关闭 SysTick 定时器
    SysTick->LOAD = 0U; // 清空 SysTick 重载寄存器
    SysTick->VAL = 0U;  // 清空 SysTick当前计数值

    /* 清除NVIC全部中断使能位、中断挂起位，避免残留中断影响 APP */
    for (uint32_t i = 0U; i < 8U; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFFU; // 清除中断使能位，禁用所有中断
        NVIC->ICPR[i] = 0xFFFFFFFFU; // 清除中断挂起位，清除所有中断挂起标志
    }

    *(__IO uint32_t *)BOOT_JUNP_FLAG_ADDR = BOOT_JUNP_MAGIC; // 写入跳转魔术标记，APP 可读取识别是 bootloader 跳转入 APP 后启动

    SCB->VTOR = CM7_APP_FLASH_BASE; // 设置向量表偏移寄存器，指向 APP 的向量表
    __set_MSP(app_stack);           // 重置主栈指针为 APP 定义的栈顶地址
    __set_CONTROL(0U);              // 复位 CONTROL 寄存器，使用 MSP 主栈，放弃 PSP
    __DSB();                        // 数据同步屏障，保证前面内存访问全部完成
    __ISB();                        // 指令同步屏障，刷新流水线，确保后续指令在新环境执行

    /**
     * jump() 后就是 APP 启动的完整流程：
     * Reset_Handler
     * → SystemInit()
     * → 拷贝 .data、清零 .bss
     * → __libc_init_array()
     * → main()
     */
    jump(); // 跳转到 APP 复位入口 Reset_Handler，不再返回
}
