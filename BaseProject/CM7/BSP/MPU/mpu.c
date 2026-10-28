/**
 * @file    mpu.c
 * @brief   STM32H747 CM7 MPU 区域表（配合 LwIP ETH DMA、LTDC 帧缓冲、FMC SDRAM）
 * @note    调用时机：建议在开启 I/D-Cache 之前或紧随其后、使用 ETH/LCD 之前完成。
 *          Region 编号从 1 起用（保留 0 给默认/其它用途）。
 *
 * 本工程关键区域一览：
 *  | Region | 基址       | 大小   | Cache | 用途说明                          |
 *  |--------|------------|--------|-------|-----------------------------------|
 *  | 1      | 0x20000000 | 128KB  | 开    | DTCM RAM                          |
 *  | 2      | 0x24000000 | 512KB  | 开    | AXI SRAM（D1）                    |
 *  | 3      | 0x30000000 | 512KB  | 关    | SRAM1/2/3（D2），ETH DMA 描述符等 |
 *  | 4      | 0x38000000 | 64KB   | 关    | SRAM4（D3）OpenAMP 共享，须非 Cache |
 *  | 5      | 0x60000000 | 64MB   | 关    | FMC NOR/PSRAM 等外设窗口          |
 *  | 6      | 0xD0000000 | 32MB   | 开    | FMC Bank2 SDRAM（LCD 帧缓冲）     |
 *  | 7      | 0x80000000 | 256MB  | 关    | QUADSPI 映射区（禁执行）          |
 */

#include "mpu.h"

/**
 * @brief   设置 MPU 区域保护
 * @param   num     MPU 编号
 * @param   baseaddr 基址
 * @param   size    大小
 * @param   sub     禁用子区域
 * @param   tex     类型扩展字段
 * @param   ap      访问权限
 * @param   de      禁用执行
 * @param   sen     是否共享
 * @param   cen     是否缓存
 * @param   ben     是否缓冲
 * @return  0 成功
 */
static uint8_t memory_set_protection(uint32_t region_num,
                                     uint32_t baseaddr,
                                     uint32_t size,
                                     uint8_t sub,
                                     uint32_t tex,
                                     uint8_t ap,
                                     uint8_t de,
                                     uint8_t sen,
                                     uint8_t cen,
                                     uint8_t ben)
{
    MPU_Region_InitTypeDef mpu_region_init_handle;

    /* 改配置前先关掉 MPU，避免半配置状态被取指/访问命中 */
    HAL_MPU_Disable();

    mpu_region_init_handle.Number = region_num;        // region 编号
    mpu_region_init_handle.Enable = MPU_REGION_ENABLE; // 启用该 region
    mpu_region_init_handle.BaseAddress = baseaddr;     // 基址
    mpu_region_init_handle.Size = size;                // 大小
    mpu_region_init_handle.SubRegionDisable = sub;     // 禁用子区域
    mpu_region_init_handle.TypeExtField = tex;         // 类型扩展字段
    mpu_region_init_handle.AccessPermission = ap;      // 访问权限
    mpu_region_init_handle.DisableExec = de;           // 禁用执行
    mpu_region_init_handle.IsShareable = sen;          // 是否共享
    mpu_region_init_handle.IsCacheable = cen;          // 是否缓存
    mpu_region_init_handle.IsBufferable = ben;         // 是否缓冲

    HAL_MPU_ConfigRegion(&mpu_region_init_handle);
    /* 特权默认：未覆盖地址按背景 region 策略 */
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

    return 0;
}

void memory_protection_unit_init(void)
{
    /* Region1：DTCM — CPU 紧耦合，可 Cache，一般放栈/关键数据 */
    memory_set_protection(MPU_REGION_NUMBER1,
                          0x20000000U,
                          MPU_REGION_SIZE_128KB,
                          0x00U,
                          MPU_TEX_LEVEL0,
                          MPU_REGION_FULL_ACCESS,
                          MPU_INSTRUCTION_ACCESS_ENABLE,
                          MPU_ACCESS_NOT_SHAREABLE,
                          MPU_ACCESS_CACHEABLE,
                          MPU_ACCESS_BUFFERABLE);

    /* Region2：AXI SRAM — 大块应用数据，可 Cache */
    memory_set_protection(MPU_REGION_NUMBER2,
                          0x24000000U,
                          MPU_REGION_SIZE_512KB,
                          0x00U,
                          MPU_TEX_LEVEL0,
                          MPU_REGION_FULL_ACCESS,
                          MPU_INSTRUCTION_ACCESS_ENABLE,
                          MPU_ACCESS_NOT_SHAREABLE,
                          MPU_ACCESS_CACHEABLE,
                          MPU_ACCESS_BUFFERABLE);

    /* Region3：D2 SRAM — ETH DMA 描述符/RxTx 缓冲所在，必须非 Cache
     * TEX=1 + C=0 + B=0 → Normal non-cacheable，避免 CPU 与 DMA 看到不一致数据 */
    memory_set_protection(MPU_REGION_NUMBER3,
                          0x30000000U,
                          MPU_REGION_SIZE_512KB,
                          0x00U,
                          MPU_TEX_LEVEL1,
                          MPU_REGION_FULL_ACCESS,
                          MPU_INSTRUCTION_ACCESS_ENABLE,
                          MPU_ACCESS_NOT_SHAREABLE,
                          MPU_ACCESS_NOT_CACHEABLE,
                          MPU_ACCESS_NOT_BUFFERABLE);

    /* Region4：SRAM4（D3）— OpenAMP resource_table/SHM，必须非 Cache + Shareable */
    memory_set_protection(MPU_REGION_NUMBER4,
                          0x38000000U,
                          MPU_REGION_SIZE_64KB,
                          0x00U,
                          MPU_TEX_LEVEL1,
                          MPU_REGION_FULL_ACCESS,
                          MPU_INSTRUCTION_ACCESS_DISABLE,
                          MPU_ACCESS_SHAREABLE,
                          MPU_ACCESS_NOT_CACHEABLE,
                          MPU_ACCESS_NOT_BUFFERABLE);

    /* Region5：FMC 外设窗口 — Device 属性，关 Cache */
    memory_set_protection(MPU_REGION_NUMBER5,
                          0x60000000U,
                          MPU_REGION_SIZE_64MB,
                          0x00U,
                          MPU_TEX_LEVEL0,
                          MPU_REGION_FULL_ACCESS,
                          MPU_INSTRUCTION_ACCESS_ENABLE,
                          MPU_ACCESS_NOT_SHAREABLE,
                          MPU_ACCESS_NOT_CACHEABLE,
                          MPU_ACCESS_NOT_BUFFERABLE);

    /* Region6：SDRAM（LCD 帧缓冲等）— 允许 Cache；改屏后需 Clean/Invalidate
     *（见 ltdc_framebuffer_sync） */
    memory_set_protection(MPU_REGION_NUMBER6,
                          0xD0000000U,
                          MPU_REGION_SIZE_32MB,
                          0x00U,
                          MPU_TEX_LEVEL0,
                          MPU_REGION_FULL_ACCESS,
                          MPU_INSTRUCTION_ACCESS_ENABLE,
                          MPU_ACCESS_NOT_SHAREABLE,
                          MPU_ACCESS_CACHEABLE,
                          MPU_ACCESS_BUFFERABLE);

    /* Region7：QSPI 映射 — 关 Cache 且禁止执行，防止误把外设当代码跑 */
    memory_set_protection(MPU_REGION_NUMBER7,
                          0x80000000U,
                          MPU_REGION_SIZE_256MB,
                          0x00U,
                          MPU_TEX_LEVEL0,
                          MPU_REGION_FULL_ACCESS,
                          MPU_INSTRUCTION_ACCESS_DISABLE,
                          MPU_ACCESS_NOT_SHAREABLE,
                          MPU_ACCESS_NOT_CACHEABLE,
                          MPU_ACCESS_NOT_BUFFERABLE);
}
