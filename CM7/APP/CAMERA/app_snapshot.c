/* PA8 松开短按抓拍，长按两秒切换只读导出。 */
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
static uint8_t s_row[SNAPSHOT_WIDTH * 3U];
static volatile uint8_t s_requested, s_busy;
static StaticTask_t s_key_control, s_task_control;
static StackType_t s_key_stack[512], s_task_stack[1024];
static uint8_t s_file_clean = 1U;

static void button_task(void *argument)
{
    (void)argument;
    uint8_t sample = 0U, stable = 0U, held = 0U;
    uint8_t armed = HAL_GPIO_ReadPin(KEY0_GPIO_Port, KEY0_Pin) == GPIO_PIN_RESET;
    uint32_t changed = HAL_GetTick(), pressed = 0U;
    for (;;)
    {
        uint32_t now = HAL_GetTick();
        uint8_t level = HAL_GPIO_ReadPin(KEY0_GPIO_Port, KEY0_Pin) == GPIO_PIN_SET;
        if (level != sample) { sample = level; changed = now; }
        if (sample != stable && now - changed >= 30U)
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
            held = 1U;
            taskENTER_CRITICAL(); s_requested = 0U; taskEXIT_CRITICAL();
            LOG_INFO("STORAGE", "BUTTON_LONG accepted=%d", app_storage_toggle());
        }
        osDelay(10U);
    }
}

static void put_u32(uint8_t *out, uint32_t value)
{
    for (uint32_t i = 0U; i < 4U; i++) out[i] = (uint8_t)(value >> (8U * i));
}

static FRESULT write_exact(FIL *file, const void *buffer, UINT size)
{
    UINT written;
    FRESULT result = f_write(file, buffer, size, &written);
    return result == FR_OK && written != size ? FR_DISK_ERR : result;
}

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
        osDelay(1U);
    }
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
        FRESULT removed = s_file_clean ? f_unlink(path) : FR_LOCKED;
        if (removed != FR_OK) LOG_ERROR("SNAPSHOT", "SNAPSHOT_CLEANUP_FAIL path=%s FR=%d", path, removed);
        goto failed;
    }
    LOG_INFO("SNAPSHOT", "SNAPSHOT_PASS path=%s size=1152054 800x480 ms=%lu", path,
             (unsigned long)(HAL_GetTick() - start));
    return;
failed:
    LOG_ERROR("SNAPSHOT", "SNAPSHOT_FAIL stage=%s FR=%d", stage, result);
}

static void snapshot_task(void *argument)
{
    (void)argument;
    app_storage_start();
    for (;;)
    {
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

void App_Snapshot_Thread_Start(void)
{
    const osThreadAttr_t attributes = {
        .name = "snapshot", .stack_size = sizeof(s_task_stack), .priority = osPriorityBelowNormal,
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
