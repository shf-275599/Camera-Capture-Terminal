/**
 * @file    app_http_mjpeg.c
 * @brief   双观看连接共享 JPEG 编码，独立发送并跳过旧帧
 */
#include "app_http_mjpeg.h"
#include "app_hw_jpeg.h"
#include "app_ota.h"
#include "ov5640_dcmi.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "mylog.h"
#include <stdio.h>

/* 直接锁定完整采集帧，MCU D0600000..D068C9FF；
 * 三块 JPEG 缓冲 D0700000..D087FFFF，各 512 KiB，与 LCD/采集区隔离。 */
#define MJPEG_CAPACITY (512U * 1024U)
#define MJPEG_CLIENTS 2U
#define MJPEG_BUFFERS 3U

typedef struct
{
    uint32_t size;
    uint32_t sequence;
    uint8_t readers;
} mjpeg_frame_t;

typedef struct
{
    struct netconn *connection;
    TaskHandle_t task;
    StaticTask_t control;
    StackType_t stack[1024];
    uint8_t active;
    uint32_t fps_x10;
    uint32_t last_frame_tick;
} mjpeg_client_t;

static mjpeg_client_t s_clients[MJPEG_CLIENTS];
static mjpeg_frame_t s_frames[MJPEG_BUFFERS];
static int s_latest = -1;
static uint32_t s_sequence;
static volatile uint8_t s_stopping;
static volatile uint8_t s_encoding;
static StaticTask_t s_task_control;
static StackType_t s_task_stack[1024];
static TaskHandle_t s_task;

static uint8_t *mjpeg_buffer(uint32_t index)
{
    return (uint8_t *)(0xD0700000U + index * MJPEG_CAPACITY);
}

static uint32_t mjpeg_clients(void)
{
    uint32_t count = 0U;
    taskENTER_CRITICAL();
    for (uint32_t i = 0U; i < MJPEG_CLIENTS; i++) count += s_clients[i].active;
    taskEXIT_CRITICAL();
    return count;
}

/* 多观看端不累加帧率，报告最近仍在发送的最快连接。 */
uint32_t app_http_mjpeg_fps_x10(void)
{
    uint32_t fps = 0U;
    taskENTER_CRITICAL();
    for (uint32_t i = 0U; i < MJPEG_CLIENTS; i++)
    {
        mjpeg_client_t *client = &s_clients[i];
        if (client->active && HAL_GetTick() - client->last_frame_tick < 3000U &&
            client->fps_x10 > fps) fps = client->fps_x10;
    }
    taskEXIT_CRITICAL();
    return fps;
}

/* 每个发送任务独占自己的 netconn；部分写入后推进偏移，不重复发送。 */
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

static void Mjpeg_SendThread(void *argument)
{
    mjpeg_client_t *client = argument;
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
        "Cache-Control: no-store, no-cache, must-revalidate\r\n"
        "Connection: close\r\n\r\n";
    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        struct netconn *conn = client->connection;
        uint32_t sequence = 0U;
        uint32_t frames = 0U;
        uint32_t fps_tick = HAL_GetTick();
        uint32_t fps_frames = 0U;
        uint32_t waiting = fps_tick;
        netconn_set_sendtimeout(conn, 100);
        if (mjpeg_send(conn, response, sizeof(response) - 1U) != 0) goto finish;
        LOG_INFO("MJPEG", "MJPEG_STREAM_START");
        while (!s_stopping)
        {
            int index = -1;
            uint32_t size = 0U;
            taskENTER_CRITICAL();
            if (s_latest >= 0 && s_frames[s_latest].sequence != sequence)
            {
                index = s_latest;
                s_frames[index].readers++;
                sequence = s_frames[index].sequence;
                size = s_frames[index].size;
            }
            taskEXIT_CRITICAL();
            if (index < 0)
            {
                if (HAL_GetTick() - waiting >= 3000U) break;
                osDelay(1U);
                continue;
            }
            char header[128];
            int length = snprintf(header, sizeof(header),
                                  "--frame\r\nContent-Type: image/jpeg\r\n"
                                  "Content-Length: %lu\r\n\r\n", (unsigned long)size);
            int result = -1;
            if (length > 0 && (size_t)length < sizeof(header))
                result = mjpeg_send(conn, header, (size_t)length);
            for (uint32_t offset = 0U; result == 0 && offset < size;)
            {
                uint32_t count = size - offset;
                if (count > 512U) count = 512U;
                result = mjpeg_send(conn, mjpeg_buffer((uint32_t)index) + offset, count);
                offset += count;
            }
            if (result == 0) result = mjpeg_send(conn, "\r\n", 2U);
            taskENTER_CRITICAL();
            s_frames[index].readers--;
            taskEXIT_CRITICAL();
            if (result != 0) break;
            waiting = HAL_GetTick();
            fps_frames++;
            taskENTER_CRITICAL();
            client->last_frame_tick = waiting;
            if (waiting - fps_tick >= 1000U)
                client->fps_x10 = fps_frames * 10000U / (waiting - fps_tick);
            taskEXIT_CRITICAL();
            if (waiting - fps_tick >= 1000U)
            {
                fps_frames = 0U;
                fps_tick = waiting;
            }
            if (++frames == 1U)
                LOG_INFO("MJPEG", "MJPEG_FIRST_FRAME bytes=%lu", (unsigned long)size);
        }
finish:
        netconn_close(conn);
        netconn_delete(conn);
        LOG_INFO("MJPEG", "MJPEG_STREAM_STOP frames=%lu", (unsigned long)frames);
        taskENTER_CRITICAL();
        client->connection = NULL;
        client->active = 0U;
        taskEXIT_CRITICAL();
    }
}

static void Mjpeg_Thread(void *argument)
{
    (void)argument;
    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (!s_stopping && mjpeg_clients())
        {
            int index = -1;
            taskENTER_CRITICAL();
            /* 防止停流已返回后，先前被抢占的编码循环重新进入硬件。 */
            if (s_stopping)
            {
                taskEXIT_CRITICAL();
                break;
            }
            /* 最新帧始终可读，发送任务持有的旧帧绝不覆盖。 */
            for (uint32_t i = 0U; i < MJPEG_BUFFERS; i++)
            {
                if ((int)i != s_latest && !s_frames[i].readers)
                {
                    index = (int)i;
                    break;
                }
            }
            s_encoding = 1U;
            taskEXIT_CRITICAL();
            if (index >= 0)
            {
                uint32_t size;
                uint8_t *rgb;
                uint32_t started = HAL_GetTick();
                if (ov5640_dcmi_acquire_frame(&rgb) == 0)
                {
                    uint32_t acquired = HAL_GetTick();
                    int result = s_stopping ? -1 : jpeg_hw_encode(rgb, mjpeg_buffer((uint32_t)index),
                                                                MJPEG_CAPACITY, &size);
                    ov5640_dcmi_release_frame();
                    if (result == 0 && !s_stopping)
                    {
                        taskENTER_CRITICAL();
                        if (++s_sequence == 0U) ++s_sequence;
                        s_frames[index].sequence = s_sequence;
                        s_frames[index].size = size;
                        s_latest = index;
                        taskEXIT_CRITICAL();
                        if ((s_sequence % 30U) == 0U)
                            LOG_INFO("MJPEG", "MJPEG_PERF acquire=%lu encode=%lu ms/frame clients=%lu",
                                     (unsigned long)(acquired - started),
                                     (unsigned long)(HAL_GetTick() - acquired),
                                     (unsigned long)mjpeg_clients());
                    }
                    else if (!s_stopping) LOG_ERROR("MJPEG", "MJPEG_ENCODE_FAIL");
                }
            }
            s_encoding = 0U;
            osDelay(1U);
        }
    }
}

int app_http_mjpeg_start(struct netconn *conn)
{
    /* 准入与 OTA 停止均由 HTTP 主任务调用。 */
    if (s_stopping || app_ota_is_busy()) return -1;
    mjpeg_client_t *client = NULL;
    taskENTER_CRITICAL();
    for (uint32_t i = 0U; i < MJPEG_CLIENTS; i++)
        if (!s_clients[i].active) { client = &s_clients[i]; break; }
    taskEXIT_CRITICAL();
    if (client == NULL) return -1;
    if (s_task == NULL)
    {
        s_task = xTaskCreateStatic(Mjpeg_Thread, "Mjpeg_Encode", 1024U, NULL, 4U,
                                  s_task_stack, &s_task_control);
        if (s_task == NULL) return -1;
    }
    if (client->task == NULL)
    {
        client->task = xTaskCreateStatic(Mjpeg_SendThread, "Mjpeg_Send", 1024U, client, 4U,
                                        client->stack, &client->control);
        if (client->task == NULL) return -1;
    }
    taskENTER_CRITICAL();
    /* 从无人观看恢复时不发送上一次会话残留的图像。 */
    if (!s_clients[0].active && !s_clients[1].active) s_latest = -1;
    client->connection = conn;
    client->fps_x10 = 0U;
    client->last_frame_tick = HAL_GetTick();
    client->active = 1U;
    taskEXIT_CRITICAL();
    xTaskNotifyGive(client->task);
    xTaskNotifyGive(s_task);
    return 0;
}

int app_http_mjpeg_stop(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    s_stopping = 1U;
    while (mjpeg_clients() || s_encoding)
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
