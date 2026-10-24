/* 同步轮询 SD 适配；FatFs 与 USB 由存储管理模块独占交接。 */
#include "app_sd.h"
#include "sdmmc.h"
#include "cmsis_os2.h"
#include "mylog.h"
#include <string.h>

#define SD_TIMEOUT_MS 1000U
static DSTATUS s_status = STA_NOINIT;
static uint32_t s_sector[512U / 4U];

static void stop_transfer(void)
{
    s_status = STA_NOINIT;
    __HAL_RCC_SDMMC2_FORCE_RESET();
    __DSB();
    __HAL_RCC_SDMMC2_RELEASE_RESET();
}

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

DSTATUS app_sd_status(BYTE drive)
{
    return drive == 0U ? s_status : STA_NOINIT;
}

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
        osThreadYield();
    }
    return RES_OK;
}

DRESULT app_sd_read(BYTE drive, BYTE *buffer, DWORD sector, UINT count)
{
    return transfer(drive, buffer, sector, count, 0);
}

DRESULT app_sd_write(BYTE drive, const BYTE *buffer, DWORD sector, UINT count)
{
    return transfer(drive, (BYTE *)buffer, sector, count, 1);
}

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
