/* FatFs 由抓拍任务使用；USB 栈和读写盘由 USB 任务串行执行。 */
#include "app_storage.h"
#include "app_usb_export.h"
#include "fatfs.h"
#include "FreeRTOS.h"
#include "task.h"
#include "mylog.h"
static volatile storage_mode_t s_mode = STORAGE_FAULT;
static uint8_t s_saving, s_toggle, s_ota, s_clean = 1U; // 保存、切换请求、OTA 占用及文件关闭状态

/** @brief 获取当前存储模式；实际操作仍须通过准入接口检查互斥状态。 */
storage_mode_t app_storage_mode(void) { return s_mode; }

/** @brief 在抓拍任务中链接并挂载 FatFs，然后启动 USB 服务任务；挂载失败不格式化。 */
void app_storage_start(void)
{
    MX_FATFS_Init();
    FRESULT result = retUSER == 0U ? f_mount(&USERFatFS, USERPath, 1U) : FR_INVALID_DRIVE;
    s_mode = result == FR_OK ? STORAGE_LOCAL : STORAGE_FAULT;
    LOG_INFO("STORAGE", "STORAGE_INIT FR=%d mode=%d (no format)", result, s_mode);
    app_usb_export_start_task();
}

/** @brief 尝试占用本地存储进行抓拍，成功返回 1，被模式或互斥状态拒绝返回 0。 */
int app_storage_snapshot_begin(void)
{
    int accepted;
    taskENTER_CRITICAL();
    accepted = s_mode == STORAGE_LOCAL && !s_saving && !s_toggle && !s_ota;
    if (accepted) s_saving = 1U;
    taskEXIT_CRITICAL();
    return accepted;
}

/**
 * @brief  释放抓拍占用，文件关闭失败时进入故障模式。
 * @param  clean 非零表示文件已安全关闭；为 0 时锁存故障，需复位恢复。
 */
void app_storage_snapshot_end(int clean)
{
    taskENTER_CRITICAL();
    s_saving = 0U;
    if (!clean) { s_clean = 0U; s_mode = STORAGE_FAULT; }
    taskEXIT_CRITICAL();
}

/** @brief 提交存储模式切换请求；接受返回 1，OTA 占用或已有切换时返回 0。 */
int app_storage_toggle(void)
{
    int accepted;
    taskENTER_CRITICAL();
    accepted = !s_ota && !s_toggle && s_mode != STORAGE_SWITCHING;
    if (accepted) s_toggle = 1U;
    taskEXIT_CRITICAL();
    return accepted;
}

/**
 * @brief  在抓拍任务中处理存储切换，同步保存结束后才交接所有权。
 * @return 未处理请求返回 0；已处理切换请求返回 1，不代表切换成功，结果见模式和日志。
 */
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
        /* 先卸载本地文件系统，再允许 USB 主机访问原始扇区。 */
        FRESULT result = f_mount(NULL, USERPath, 0U);
        if (result == FR_OK && app_usb_export_set(1) == 0)
        {
            s_mode = STORAGE_USB;
            LOG_INFO("STORAGE", "USB_EXPORT_READY readonly=0");
            return 1;
        }
        LOG_ERROR("STORAGE", "USB_EXPORT_START_FAIL unmount=%d", result);
    }
    /* 返回本地模式前必须确认 USB 已停止访问，失败时不重新挂载 FatFs。 */
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

/** @brief 尝试占用存储以阻止抓拍和模式切换；成功返回 0，忙或非本地模式返回 -1。 */
int app_storage_ota_lock(void)
{
    int accepted;
    taskENTER_CRITICAL();
    accepted = s_mode == STORAGE_LOCAL && !s_saving && !s_toggle && !s_ota;
    if (accepted) s_ota = 1U;
    taskEXIT_CRITICAL();
    return accepted ? 0 : -1;
}

/** @brief 释放 OTA 占用，允许后续抓拍或存储切换请求。 */
void app_storage_ota_unlock(void)
{
    taskENTER_CRITICAL(); s_ota = 0U; taskEXIT_CRITICAL();
}
