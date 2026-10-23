/**
 * @file    app_http_mjpeg.c
 * @brief   单观看连接的 HTTP MJPEG 推流，独立于 HTTP/OTA 请求任务
 */
#include "app_http_mjpeg.h"
#include "app_http_util.h"
#include "app_hw_jpeg.h"
#include "app_ota.h"
#include "ov5640_dcmi.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "mylog.h"
#include <stdio.h>

/* 独立 SDRAM 区：快照 D0500000..D05BB7FF，MCU D0600000..D068C9FF，
 * JPEG D0700000..D077FFFF；避开 LCD、相机三帧与旧解码工作区。 */
#define MJPEG_RGB ((uint8_t *)0xD0500000U)
#define MJPEG_OUTPUT ((uint8_t *)0xD0700000U)
#define MJPEG_CAPACITY (512U * 1024U)

static volatile uint8_t s_active;
static volatile uint8_t s_stopping;
static StaticTask_t s_task_control;
static StackType_t s_task_stack[1024];
static TaskHandle_t s_task;
static struct netconn *s_connection;
static uint32_t s_fps_x10;
static uint32_t s_last_frame_tick;

uint32_t app_http_mjpeg_fps_x10(void)
{
    uint32_t fps;
    taskENTER_CRITICAL();
    fps = (s_active && HAL_GetTick() - s_last_frame_tick < 3000U) ? s_fps_x10 : 0U;
    taskEXIT_CRITICAL();
    return fps;
}

/* 发送窗口暂满时等待 ACK；已有部分写入必须推进偏移，避免重复字节。 */
static int mjpeg_send(struct netconn *conn, const void *data, size_t length)
{
    const uint8_t *bytes = data;
    uint32_t progress = HAL_GetTick();
    while (length && !s_stopping)
    {
        size_t written = 0U;
        err_t error = netconn_write_partly(conn, bytes, length, NETCONN_COPY, &written);
        if (written > length) return -1;
        bytes += written;
        length -= written;
        if (written) progress = HAL_GetTick();
        if (error != ERR_OK && error != ERR_WOULDBLOCK && error != ERR_TIMEOUT) return -1;
        if (HAL_GetTick() - progress >= 3000U) return -1;
        if (length) osDelay(1U);
    }
    return length ? -1 : 0;
}

static void Mjpeg_Thread(void *argument)
{
    (void)argument;
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
        "Cache-Control: no-store, no-cache, must-revalidate\r\n"
        "Connection: close\r\n\r\n";
    for (;;)
    {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    struct netconn *conn = s_connection;
    uint32_t frames = 0U;
    uint32_t waiting = HAL_GetTick();
    uint32_t fps_tick = waiting;
    uint32_t fps_frames = 0U;
    netconn_set_sendtimeout(conn, 1000);
    if (mjpeg_send(conn, response, sizeof(response) - 1U) != 0) goto finish;
    LOG_INFO("CM7", "MJPEG_STREAM_START");
    while (!s_stopping)
    {
        char header[128];
        uint32_t size;
        uint32_t started = HAL_GetTick();
        if (ov5640_dcmi_copy_frame(MJPEG_RGB) != 0)
        {
            if (HAL_GetTick() - waiting > 3000U) break;
            osDelay(10U);
            continue;
        }
        waiting = HAL_GetTick();
        if (s_stopping) break;
        if (jpeg_hw_encode(MJPEG_RGB, MJPEG_OUTPUT, MJPEG_CAPACITY, &size) != 0)
        {
            LOG_ERROR("CM7", "MJPEG_ENCODE_FAIL");
            break;
        }
        if (s_stopping) break;
        int length = snprintf(header, sizeof(header),
                              "--frame\r\nContent-Type: image/jpeg\r\n"
                              "Content-Length: %lu\r\n\r\n", (unsigned long)size);
        if (length <= 0 || (size_t)length >= sizeof(header) ||
            mjpeg_send(conn, header, (size_t)length) != 0) break;
        /* 分块检查停止标志，OTA 无需等待一整帧慢速发送。 */
        uint32_t offset = 0U;
        while (offset < size && !s_stopping)
        {
            uint32_t count = size - offset;
            if (count > 512U) count = 512U;
            if (mjpeg_send(conn, MJPEG_OUTPUT + offset, count) != 0) goto finish;
            offset += count;
        }
        if (s_stopping || mjpeg_send(conn, "\r\n", 2U) != 0) break;
        uint32_t now = HAL_GetTick();
        fps_frames++;
        taskENTER_CRITICAL();
        s_last_frame_tick = now;
        if (now - fps_tick >= 1000U)
        {
            s_fps_x10 = fps_frames * 10000U / (now - fps_tick);
            fps_frames = 0U;
            fps_tick = now;
        }
        taskEXIT_CRITICAL();
        if (++frames == 1U)
            LOG_INFO("CM7", "MJPEG_FIRST_FRAME bytes=%lu", (unsigned long)size);
        while (!s_stopping && HAL_GetTick() - started < 100U) osDelay(1U);
    }
finish:
    netconn_close(conn);
    netconn_delete(conn);
    LOG_INFO("CM7", "MJPEG_STREAM_STOP frames=%lu", (unsigned long)frames);
    __DMB();
    s_active = 0U;
    }
}

int app_http_mjpeg_start(struct netconn *conn)
{
    /* 只有 HTTP 主任务启动连接，停止标志也由该任务通过 OTA 设置。 */
    if (s_active || s_stopping || app_ota_is_busy()) return -1;
    if (s_task == NULL)
    {
        /* 原工程 RTOS 堆仅 15 KiB，使用固定栈，避免观看连接耗尽动态堆。 */
        s_task = xTaskCreateStatic(Mjpeg_Thread, "Mjpeg_Thread", 1024U, NULL, 4U,
                                  s_task_stack, &s_task_control);
        if (s_task == NULL) return -1;
    }
    s_connection = conn;
    s_fps_x10 = 0U;
    s_last_frame_tick = HAL_GetTick();
    s_active = 1U;
    xTaskNotifyGive(s_task);
    return 0;
}

int app_http_mjpeg_stop(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    s_stopping = 1U;
    while (s_active)
    {
        if (HAL_GetTick() - start >= timeout_ms) return -1;
        osDelay(1U);
    }
    return 0;
}

void app_http_mjpeg_resume(void)
{
    s_stopping = 0U;
}
