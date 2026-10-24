/* FatFs 由抓拍任务使用；USB 栈和读盘由 USB 任务串行执行。 */
#include "app_storage.h"
#include "app_usb_export.h"
#include "fatfs.h"
#include "FreeRTOS.h"
#include "task.h"
#include "mylog.h"
static volatile storage_mode_t s_mode = STORAGE_FAULT;
static uint8_t s_saving, s_toggle, s_ota, s_clean = 1U;

storage_mode_t app_storage_mode(void) { return s_mode; }

void app_storage_start(void)
{
    MX_FATFS_Init();
    FRESULT result = retUSER == 0U ? f_mount(&USERFatFS, USERPath, 1U) : FR_INVALID_DRIVE;
    s_mode = result == FR_OK ? STORAGE_LOCAL : STORAGE_FAULT;
    LOG_INFO("STORAGE", "STORAGE_INIT FR=%d mode=%d (no format)", result, s_mode);
    app_usb_export_start_task();
}

int app_storage_snapshot_begin(void)
{
    int accepted;
    taskENTER_CRITICAL();
    accepted = s_mode == STORAGE_LOCAL && !s_saving && !s_toggle && !s_ota;
    if (accepted) s_saving = 1U;
    taskEXIT_CRITICAL();
    return accepted;
}

void app_storage_snapshot_end(int clean)
{
    taskENTER_CRITICAL();
    s_saving = 0U;
    if (!clean) { s_clean = 0U; s_mode = STORAGE_FAULT; }
    taskEXIT_CRITICAL();
}

int app_storage_toggle(void)
{
    int accepted;
    taskENTER_CRITICAL();
    accepted = !s_ota && !s_toggle && s_mode != STORAGE_SWITCHING;
    if (accepted) s_toggle = 1U;
    taskEXIT_CRITICAL();
    return accepted;
}

/* 同步保存返回且文件关闭之后才交接所有权。 */
int app_storage_service(void)
{
    storage_mode_t previous;
    taskENTER_CRITICAL();
    if (!s_toggle || s_saving || s_ota) { taskEXIT_CRITICAL(); return 0; }
    s_toggle = 0U;
    previous = s_mode;
    s_mode = STORAGE_SWITCHING;
    taskEXIT_CRITICAL();
    LOG_INFO("STORAGE", "STORAGE_SWITCH_START previous=%d", previous);
    if (previous == STORAGE_LOCAL)
    {
        FRESULT result = f_mount(NULL, USERPath, 0U);
        if (result == FR_OK && app_usb_export_set(1) == 0)
        {
            s_mode = STORAGE_USB;
            LOG_INFO("STORAGE", "USB_EXPORT_READY readonly=1");
            return 1;
        }
        LOG_ERROR("STORAGE", "USB_EXPORT_START_FAIL unmount=%d", result);
    }
    if (app_usb_export_set(0) != 0)
    {
        s_mode = STORAGE_FAULT;
        LOG_ERROR("STORAGE", "USB_EXPORT_STOP_FAIL ownership=usb");
        return 1;
    }
    if (!s_clean)
    {
        s_mode = STORAGE_FAULT;
        LOG_ERROR("STORAGE", "STORAGE_RECOVERY_FAIL unclosed_file reset_required");
        return 1;
    }
    /* Cube 的 disk_initialize 缓存初始化标记；重新链接后才能从 I/O 故障恢复。 */
    FRESULT result = f_mount(NULL, USERPath, 0U);
    if (result == FR_OK)
    {
        result = FATFS_UnLinkDriver(USERPath) == 0U ? FR_OK : FR_INVALID_DRIVE;
        if (result == FR_OK)
        {
            MX_FATFS_Init();
            result = retUSER == 0U ? f_mount(&USERFatFS, USERPath, 1U) : FR_INVALID_DRIVE;
        }
    }
    s_mode = result == FR_OK ? STORAGE_LOCAL : STORAGE_FAULT;
    LOG_INFO("STORAGE", "STORAGE_LOCAL FR=%d mode=%d", result, s_mode);
    return 1;
}

int app_storage_ota_lock(void)
{
    int accepted;
    taskENTER_CRITICAL();
    accepted = s_mode == STORAGE_LOCAL && !s_saving && !s_toggle && !s_ota;
    if (accepted) s_ota = 1U;
    taskEXIT_CRITICAL();
    return accepted ? 0 : -1;
}

void app_storage_ota_unlock(void)
{
    taskENTER_CRITICAL(); s_ota = 0U; taskEXIT_CRITICAL();
}
