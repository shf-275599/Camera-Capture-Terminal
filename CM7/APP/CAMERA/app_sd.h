#ifndef APP_SD_H
#define APP_SD_H
#include "ff_gen_drv.h"
DSTATUS app_sd_initialize(BYTE drive);
DSTATUS app_sd_status(BYTE drive);
DRESULT app_sd_read(BYTE drive, BYTE *buffer, DWORD sector, UINT count);
DRESULT app_sd_write(BYTE drive, const BYTE *buffer, DWORD sector, UINT count);
DRESULT app_sd_ioctl(BYTE drive, BYTE command, void *buffer);
#endif
