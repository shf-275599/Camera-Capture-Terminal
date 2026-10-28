#ifndef APP_SD_H
#define APP_SD_H
#include "ff_gen_drv.h"
/* 仅支持驱动 0；读写及初始化须在任务上下文串行调用，扇区大小固定为 512 字节。 */
/** @brief 初始化 SD 卡，成功返回 0，失败返回 STA_NOINIT，不格式化介质。 */
DSTATUS app_sd_initialize(BYTE drive);
/** @brief 返回缓存的初始化状态，无效驱动号返回 STA_NOINIT。 */
DSTATUS app_sd_status(BYTE drive);
/** @brief 从 sector 起读取 count 个扇区到 buffer；成功返回 RES_OK，否则返回磁盘错误码。 */
DRESULT app_sd_read(BYTE drive, BYTE *buffer, DWORD sector, UINT count);
/** @brief 将 buffer 中 count 个扇区写入 sector 起的区域；成功返回 RES_OK，否则返回磁盘错误码。 */
DRESULT app_sd_write(BYTE drive, const BYTE *buffer, DWORD sector, UINT count);
/** @brief 同步介质或查询容量；查询缓冲区按命令提供 WORD/DWORD，成功返回 RES_OK。 */
DRESULT app_sd_ioctl(BYTE drive, BYTE command, void *buffer);
#endif
