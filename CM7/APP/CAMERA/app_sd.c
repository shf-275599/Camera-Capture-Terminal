/* 同步轮询 SD 适配；FatFs 与 USB 由存储管理模块独占交接。 */
#include "app_sd.h"
#include "sdmmc.h"
#include "cmsis_os2.h"
#include "mylog.h"
#include <string.h>

#define SD_TIMEOUT_MS 1000U
static DSTATUS s_status = STA_NOINIT;
static uint32_t s_sector[512U / 4U]; // 字对齐的单扇区中转区，调用方须串行访问

/** @brief 复位 SDMMC2 并标记未初始化，阻止故障后继续使用未恢复的控制器。 */
static void stop_transfer(void)
{
    s_status = STA_NOINIT;
    __HAL_RCC_SDMMC2_FORCE_RESET();
    __DSB();
    __HAL_RCC_SDMMC2_RELEASE_RESET();
}

/** @brief 等待卡返回可传输状态，等待时阻塞让出 CPU；超时返回 RES_ERROR。 */
static DRESULT wait_ready(void)
{
    uint32_t start = HAL_GetTick();
    while (HAL_SD_GetCardState(&hsd2) != HAL_SD_CARD_TRANSFER)
    {
        if (HAL_GetTick() - start >= SD_TIMEOUT_MS) return RES_ERROR;
        osDelay(1U);
    }
    return RES_OK;
}

/**
 * @brief  在任务上下文初始化 SDMMC2 和 SD 卡，不执行格式化。
 * @param  drive 物理驱动号，仅支持 0。
 * @return 初始化成功返回 0，否则返回 STA_NOINIT。
 */
DSTATUS app_sd_initialize(BYTE drive)
{
    if (drive != 0U || __get_IPSR() != 0U) return STA_NOINIT;
    s_status = STA_NOINIT;
    if (hsd2.Instance != NULL) (void)HAL_SD_DeInit(&hsd2);
    stop_transfer();
    MX_SDMMC2_SD_Init();
    if (hsd2.ErrorCode == HAL_SD_ERROR_NONE &&
        HAL_SD_GetState(&hsd2) == HAL_SD_STATE_READY && wait_ready() == RES_OK)
        s_status = 0U;
    if (s_status != 0U) stop_transfer();
    LOG_INFO("STORAGE", "SD_INIT status=%u sectors=%lu HAL=0x%lX", s_status,
             (unsigned long)hsd2.SdCard.LogBlockNbr, (unsigned long)hsd2.ErrorCode);
    return s_status;
}

/** @brief 返回缓存的驱动初始化状态；无效驱动号返回 STA_NOINIT，不主动探测卡。 */
DSTATUS app_sd_status(BYTE drive)
{
    return drive == 0U ? s_status : STA_NOINIT;
}

/**
 * @brief  使用字对齐中转区逐扇区同步读写，失败时复位控制器。
 * @param  drive 物理驱动号，仅支持 0。
 * @param  buffer 至少 count * 512 字节的读写缓冲区。
 * @param  sector 起始逻辑扇区号。
 * @param  count 扇区数，必须大于 0 且不超过卡容量边界。
 * @param  writing 非零表示写入，0 表示读取。
 * @return 成功返回 RES_OK，否则返回参数、未就绪或 I/O 错误。
 * @note   仅允许任务上下文调用，由存储所有权管理保证 FatFs 与 USB 不并发访问。
 */
static DRESULT transfer(BYTE drive, BYTE *buffer, DWORD sector, UINT count, int writing)
{
    if (drive != 0U || buffer == NULL || count == 0U || __get_IPSR() != 0U) return RES_PARERR;
    if (s_status != 0U) return RES_NOTRDY;
    if (sector >= hsd2.SdCard.LogBlockNbr || count > hsd2.SdCard.LogBlockNbr - sector) return RES_PARERR;
    while (count-- != 0U)
    {
        HAL_StatusTypeDef status;
        if (writing)
        {
            memcpy(s_sector, buffer, 512U);
            status = HAL_SD_WriteBlocks(&hsd2, (uint8_t *)s_sector, sector, 1U, SD_TIMEOUT_MS);
        }
        else
            status = HAL_SD_ReadBlocks(&hsd2, (uint8_t *)s_sector, sector, 1U, SD_TIMEOUT_MS);
        /* HAL 数据传输结束后仍需等待卡内部操作完成，才能开始下一扇区。 */
        if (status != HAL_OK || wait_ready() != RES_OK)
        {
            LOG_ERROR("STORAGE", "SD_IO_FAIL write=%d sector=%lu HAL=0x%lX",
                      writing, (unsigned long)sector, (unsigned long)hsd2.ErrorCode);
            stop_transfer();
            return RES_ERROR;
        }
        if (!writing) memcpy(buffer, s_sector, 512U);
        buffer += 512U;
        sector++;
        osThreadYield(); // 扇区之间向同优先级就绪任务让出执行机会
    }
    return RES_OK;
}

/** @brief 从 sector 起读取 count 个 512 字节扇区；参数和返回值约定见 transfer。 */
DRESULT app_sd_read(BYTE drive, BYTE *buffer, DWORD sector, UINT count)
{
    return transfer(drive, buffer, sector, count, 0);
}

/** @brief 向 sector 起写入 count 个 512 字节扇区；参数和返回值约定见 transfer。 */
DRESULT app_sd_write(BYTE drive, const BYTE *buffer, DWORD sector, UINT count)
{
    return transfer(drive, (BYTE *)buffer, sector, count, 1);
}

/**
 * @brief  处理 FatFs 同步及容量查询命令，未知命令返回 RES_PARERR。
 * @param  drive 物理驱动号，仅支持 0。
 * @param  command CTRL_SYNC、GET_SECTOR_COUNT、GET_SECTOR_SIZE 或 GET_BLOCK_SIZE。
 * @param  buffer 查询结果地址，容量/块大小用 DWORD，扇区大小用 WORD；同步命令不使用。
 * @return 成功返回 RES_OK，未初始化返回 RES_NOTRDY，同步失败返回 RES_ERROR。
 */
DRESULT app_sd_ioctl(BYTE drive, BYTE command, void *buffer)
{
    if (drive != 0U) return RES_PARERR;
    if (s_status != 0U) return RES_NOTRDY;
    switch (command)
    {
        case CTRL_SYNC:
            if (wait_ready() == RES_OK) return RES_OK;
            stop_transfer();
            return RES_ERROR;
        case GET_SECTOR_COUNT:
            if (buffer == NULL) return RES_PARERR;
            *(DWORD *)buffer = hsd2.SdCard.LogBlockNbr; return RES_OK;
        case GET_SECTOR_SIZE:
            if (buffer == NULL) return RES_PARERR;
            *(WORD *)buffer = 512U; return RES_OK;
        case GET_BLOCK_SIZE:
            if (buffer == NULL) return RES_PARERR;
            *(DWORD *)buffer = 1U; return RES_OK;
        default: return RES_PARERR;
    }
}
