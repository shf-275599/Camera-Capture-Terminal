/* PA8 松开短按抓拍，长按两秒切换 USB 读写导出。 */
#include "app_snapshot.h"
#include "main.h"
#include "cmsis_os2.h"
#include "ov5640_dcmi.h"
#include "app_ota.h"
#include "app_storage.h"
#include "ff.h"
#include "mylog.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

#define SNAPSHOT_WIDTH 800U
#define SNAPSHOT_HEIGHT 480U
/* 独立 CPU 快照，避开 LCD、相机三帧、JPEG 和 MJPEG。 */
#define SNAPSHOT_FRAME ((uint16_t *)0xD0900000U)
static uint8_t s_row[SNAPSHOT_WIDTH * 3U]; // 单行 BGR888 数据，800 像素恰好满足 BMP 四字节对齐
static volatile uint8_t s_requested, s_busy;
static StaticTask_t s_key_control, s_task_control;
static StackType_t s_key_stack[512], s_task_stack[1024];
static uint8_t s_file_clean = 1U; // 关闭文件失败后保持为 0，禁止继续交接存储所有权

/**
 * @brief  轮询按键并消抖，松开短按提交抓拍，长按两秒请求切换存储模式。
 * @param  argument 未使用的任务参数。
 * @note   只提交请求，文件操作由抓拍任务串行执行；长按松开时不再触发抓拍。
 */
static void button_task(void *argument)
{
    (void)argument;
    uint8_t sample = 0U, stable = 0U, held = 0U;
    uint8_t armed = HAL_GPIO_ReadPin(KEY0_GPIO_Port, KEY0_Pin) == GPIO_PIN_RESET; // 上电已按住时，先松开再接受操作
    uint32_t changed = HAL_GetTick(), pressed = 0U;
    for (;;)
    {
        uint32_t now = HAL_GetTick();
        uint8_t level = HAL_GPIO_ReadPin(KEY0_GPIO_Port, KEY0_Pin) == GPIO_PIN_SET;
        if (level != sample) { sample = level; changed = now; }
        if (sample != stable && now - changed >= 30U) // 电平持续稳定 30 ms 才接受边沿
        {
            stable = sample;
            if (stable)
            {
                pressed = now;
                held = app_storage_mode() == STORAGE_SWITCHING;
            }
            else
            {
                if (armed && !held)
                {
                    taskENTER_CRITICAL();
                    uint8_t accepted = !s_busy && app_storage_mode() == STORAGE_LOCAL;
                    if (accepted) s_requested = 1U;
                    taskEXIT_CRITICAL();
                    if (!accepted) LOG_INFO("SNAPSHOT", "SNAPSHOT_BUSY mode=%d", app_storage_mode());
                }
                armed = 1U;
            }
        }
        if (stable && armed && !held && now - pressed >= 2000U)
        {
            held = 1U; // 本次长按只处理一次，并抑制松开后的短按动作
            taskENTER_CRITICAL(); s_requested = 0U; taskEXIT_CRITICAL();
            LOG_INFO("STORAGE", "BUTTON_LONG accepted=%d", app_storage_toggle());
        }
        osDelay(10U);
    }
}

/**
 * @brief  将 32 位数按 BMP 要求的小端序写入四字节缓冲区。
 * @param  out 至少可写四字节的目标地址。
 * @param  value 待写入的数值。
 */
static void put_u32(uint8_t *out, uint32_t value)
{
    for (uint32_t i = 0U; i < 4U; i++) out[i] = (uint8_t)(value >> (8U * i));
}

/**
 * @brief  写入指定字节数，将 FatFs 返回成功但写入不足视为磁盘错误。
 * @param  file 已打开的文件。
 * @param  buffer 待写入数据。
 * @param  size 请求写入的字节数。
 * @return 完整写入返回 FR_OK，否则返回 FatFs 错误或 FR_DISK_ERR。
 */
static FRESULT write_exact(FIL *file, const void *buffer, UINT size)
{
    UINT written;
    FRESULT result = f_write(file, buffer, size, &written);
    return result == FR_OK && written != size ? FR_DISK_ERR : result;
}

/**
 * @brief  复制完整相机帧，以未占用的 CAM0001～CAM9999 文件名保存 24 位 BMP。
 * @note   调用者须先取得本地存储使用权；错误通过日志报告，关闭失败会锁存不干净状态。
 */
static void take_snapshot(void)
{
    uint32_t start = HAL_GetTick();
    uint8_t header[54] = {'B', 'M'};
    FIL file;
    FILINFO info;
    char path[20] = {0};
    const char *stage = "camera";
    FRESULT result;
    LOG_INFO("SNAPSHOT", "SNAPSHOT_START");
    uint32_t copy_start = HAL_GetTick();
    int copied = -1;
    /* 与 MJPEG 竞争取帧时有限重试；复制完成后只读取独立快照，不长期持有相机帧。 */
    while (!app_ota_is_busy())
    {
        copied = ov5640_dcmi_copy_frame(SNAPSHOT_FRAME);
        if (copied == 0 || HAL_GetTick() - copy_start >= 1000U) break;
        osDelay(5U);
    }
    if (copied != 0 || app_ota_is_busy())
    {
        LOG_ERROR("SNAPSHOT", "SNAPSHOT_FAIL stage=camera busy_or_timeout");
        return;
    }
    /* 查找首个空闲编号，并用 CREATE_NEW 防止覆盖已有照片。 */
    stage = "filename";
    for (uint32_t index = 1U; index <= 9999U; index++)
    {
        snprintf(path, sizeof(path), "0:/CAM%04lu.BMP", (unsigned long)index);
        result = f_stat(path, &info);
        if (result == FR_NO_FILE) break;
        if (result != FR_OK) goto failed;
    }
    if (result != FR_NO_FILE) { result = FR_EXIST; goto failed; }
    stage = "open";
    result = f_open(&file, path, FA_WRITE | FA_CREATE_NEW);
    if (result != FR_OK) goto failed;
    /* 14 字节文件头 + 40 字节信息头；正高度表示像素从底行开始存储。 */
    put_u32(header + 2, 54U + sizeof(s_row) * SNAPSHOT_HEIGHT);
    put_u32(header + 10, 54U);
    put_u32(header + 14, 40U);
    put_u32(header + 18, SNAPSHOT_WIDTH);
    put_u32(header + 22, SNAPSHOT_HEIGHT);
    header[26] = 1U;
    header[28] = 24U;
    put_u32(header + 34, sizeof(s_row) * SNAPSHOT_HEIGHT);
    stage = "header";
    result = write_exact(&file, header, sizeof(header));
    if (result == FR_OK) stage = "pixels";
    /* RGB565 扩展为 BGR888，逐行写入，避免额外分配整幅 24 位图像。 */
    for (uint32_t y = SNAPSHOT_HEIGHT; y != 0U && result == FR_OK; y--)
    {
        const uint16_t *source = SNAPSHOT_FRAME + (y - 1U) * SNAPSHOT_WIDTH;
        for (uint32_t x = 0U; x < SNAPSHOT_WIDTH; x++)
        {
            uint16_t pixel = source[x];
            uint8_t b = pixel & 31U, g = (pixel >> 5) & 63U, r = pixel >> 11;
            s_row[x * 3U] = (b << 3) | (b >> 2);
            s_row[x * 3U + 1U] = (g << 2) | (g >> 4);
            s_row[x * 3U + 2U] = (r << 3) | (r >> 2);
        }
        result = write_exact(&file, s_row, sizeof(s_row));
        osDelay(1U); // 每行写完后阻塞让出 CPU，保持预览任务可运行
    }
    /* 已打开的文件无论写入是否成功都要关闭，保留最先发生的错误。 */
    if (result == FR_OK) { stage = "sync"; result = f_sync(&file); }
    {
        FRESULT closed = f_close(&file);
        if (closed != FR_OK)
        {
            s_file_clean = 0U;
            LOG_ERROR("SNAPSHOT", "SNAPSHOT_CLOSE_FAIL FR=%d", closed);
            if (result == FR_OK) { stage = "close"; result = closed; }
        }
    }
    if (result != FR_OK)
    {
        FRESULT removed = s_file_clean ? f_unlink(path) : FR_LOCKED; // 仅在文件已安全关闭时删除不完整照片
        if (removed != FR_OK) LOG_ERROR("SNAPSHOT", "SNAPSHOT_CLEANUP_FAIL path=%s FR=%d", path, removed);
        goto failed;
    }
    LOG_INFO("SNAPSHOT", "SNAPSHOT_PASS path=%s size=1152054 800x480 ms=%lu", path,
             (unsigned long)(HAL_GetTick() - start));
    return;
failed:
    LOG_ERROR("SNAPSHOT", "SNAPSHOT_FAIL stage=%s FR=%d", stage, result);
}

/**
 * @brief  初始化存储，串行处理抓拍请求和保存结束后的存储模式切换。
 * @param  argument 未使用的任务参数。
 */
static void snapshot_task(void *argument)
{
    (void)argument;
    app_storage_start();
    for (;;)
    {
        /* 与按键任务原子交接请求，避免读取后清零时丢失新请求。 */
        taskENTER_CRITICAL();
        uint8_t requested = s_requested;
        s_requested = 0U;
        s_busy = requested;
        taskEXIT_CRITICAL();
        if (requested && app_storage_snapshot_begin())
        {
            take_snapshot();
            app_storage_snapshot_end(s_file_clean);
        }
        s_busy = 0U;
        (void)app_storage_service();
        osDelay(20U);
    }
}

/**
 * @brief  创建使用静态任务栈的抓拍任务和按键任务，创建失败时记录日志。
 * @note   应在启动流程中调用一次，抓拍优先级低于相机与 MJPEG 任务。
 */
void App_Snapshot_Thread_Start(void)
{
    /* CMSIS 优先级直接映射到 FreeRTOS；抓拍须低于相机(5)和 MJPEG(4)。 */
    const osThreadAttr_t attributes = {
        .name = "snapshot", .stack_size = sizeof(s_task_stack), .priority = (osPriority_t)3,
        .stack_mem = s_task_stack, .cb_mem = &s_task_control, .cb_size = sizeof(s_task_control)
    };
    if (osThreadNew(snapshot_task, NULL, &attributes) == NULL)
        LOG_ERROR("SNAPSHOT", "SNAPSHOT_TASK_FAIL");
    const osThreadAttr_t key_attr = { .name = "key_mode", .priority = osPriorityNormal,
        .stack_mem = s_key_stack, .stack_size = sizeof(s_key_stack),
        .cb_mem = &s_key_control, .cb_size = sizeof(s_key_control) };
    if (osThreadNew(button_task, NULL, &key_attr) == NULL)
        LOG_ERROR("SNAPSHOT", "BUTTON_TASK_FAIL");
}
