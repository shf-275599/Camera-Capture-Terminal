/**
 * @brief HTTP OTA 管理入口；由 HTTP 任务串行处理会话和超时。
 */
#include "app_http_ota.h"
#include "app_ota.h"
#include "boot_eeprom.h"
#include "cm4_ota_hold.h"
#include "cmsis_os2.h"
#include "main.h"
#include "mylog.h"
#include <stdio.h>
#include <string.h>

#define HTTP_OTA_IDLE_MS 60000U
#define HTTP_OTA_TOTAL_MS 600000U

static uint8_t s_receiving;
static uint32_t s_started;
static uint32_t s_activity;
static uint8_t s_cm4_boot_checked;

static void ota_reply(struct netconn *conn, int code, const char *reason, const char *body)
{
    (void)app_http_reply(conn, code, reason, "text/plain; charset=utf-8", body);
}

void app_http_ota_receive_failed(void)
{
    if (s_receiving != 0U)
    {
        app_ota_abort();
        s_receiving = 0U;
        LOG_WARN("HTTP-OTA", "receive failed, session aborted");
    }
}

void app_http_ota_poll(void)
{
    uint32_t now = HAL_GetTick();
    /* CM4 任务上报实际运行槽后，由同一管理任务确认本轮启动。 */
    uint8_t running = cm4_ota_running_slot();
    if (s_cm4_boot_checked == 0U && running < CM4_APP_SLOT_COUNT &&
        !app_ota_is_busy() && app_ota_control_try_lock())
    {
        boot_eeprom_meta_t meta;
        if (boot_eeprom_load(&meta) == 0)
        {
            if ((meta.cm4_flags & BOOT_EE_FLAG_CM4_PENDING) == 0U)
                s_cm4_boot_checked = 1U;
            else if (meta.cm4_pending_slot == running && boot_eeprom_commit_cm4_pending(&meta) == 0)
            {
                s_cm4_boot_checked = 1U;
                LOG_INFO("HTTP-OTA", "CM4 pending committed -> confirmed=%u", running);
            }
        }
        app_ota_control_unlock();
    }
    if (s_receiving != 0U &&
        ((now - s_activity >= HTTP_OTA_IDLE_MS) || (now - s_started >= HTTP_OTA_TOTAL_MS)))
    {
        LOG_WARN("HTTP-OTA", "session timeout");
        app_http_ota_receive_failed();
    }
}

int app_http_ota_dispatch(struct netconn *conn, const app_http_request_t *request)
{
    app_ota_core_t core = APP_OTA_CORE_CM7;
    char text[1024];
    char core_name[16];
    int result;
    int is_status = strcmp(request->path, "/ota/status") == 0;
    int is_begin = strcmp(request->path, "/ota/begin") == 0;
    int is_chunk = strcmp(request->path, "/ota/chunk") == 0;
    int is_end = strcmp(request->path, "/ota/end") == 0;
    int is_abort = strcmp(request->path, "/ota/abort") == 0;
    if (!is_status && !is_begin && !is_chunk && !is_end && !is_abort) return 0;
    if (strcmp(request->method, is_status ? "GET" : "POST") != 0)
    {
        ota_reply(conn, 405, "Method Not Allowed", "invalid method\n");
        return 1;
    }
    if (!is_chunk && request->content_length != 0U)
    {
        ota_reply(conn, 400, "Bad Request", "body must be empty\n");
        return 1;
    }
    if (is_status || is_begin)
    {
        result = app_http_header_get(request, "X-Ota-Core", core_name, sizeof(core_name));
        if (result != 0 || (strcmp(core_name, "cm7") != 0 && strcmp(core_name, "cm4") != 0))
        {
            ota_reply(conn, 400, "Bad Request", "X-Ota-Core must be cm7 or cm4\n");
            return 1;
        }
        core = strcmp(core_name, "cm4") == 0 ? APP_OTA_CORE_CM4 : APP_OTA_CORE_CM7;
    }
    if (is_status)
    {
        result = app_ota_format_status(text, sizeof(text), core);
        if (result < 0) ota_reply(conn, 503, "Service Unavailable", "running slot unavailable\n");
        else ota_reply(conn, 200, "OK", text);
        return 1;
    }
    if (is_begin)
    {
        uint32_t size, version, crc, base;
        if (app_http_header_u32(request, "X-Fw-Size", &size) != 0 ||
            app_http_header_u32(request, "X-Fw-Version", &version) != 0 ||
            app_http_header_u32(request, "X-Fw-Crc32", &crc) != 0 ||
            app_http_header_u32(request, "X-Fw-Base", &base) != 0)
        {
            ota_reply(conn, 400, "Bad Request", "size/version/crc32/base required as u32\n");
            return 1;
        }
        if (app_ota_is_busy())
        {
            ota_reply(conn, 409, "Conflict", "OTA busy\n");
            return 1;
        }
        result = app_ota_begin(size, version, core, crc, base);
        if (result != 0)
        {
            snprintf(text, sizeof(text), "begin failed rc=%d\n", result);
            int code = result == -2 ? 413 : (result == -8 || result == -6 ? 409 : 503);
            ota_reply(conn, code, code == 413 ? "Payload Too Large" : (code == 409 ? "Conflict" : "Service Unavailable"), text);
            return 1;
        }
        s_started = s_activity = HAL_GetTick();
        s_receiving = 1U;
        if (app_http_reply(conn, 200, "OK", "text/plain; charset=utf-8", "OTA ready\n") != 0)
            app_http_ota_receive_failed();
        return 1;
    }
    if (is_abort)
    {
        if (app_ota_is_busy() && s_receiving == 0U)
        {
            ota_reply(conn, 409, "Conflict", "pending reboot; cannot abort\n");
            return 1;
        }
        app_ota_abort();
        s_receiving = 0U;
        ota_reply(conn, 200, "OK", "OTA aborted\n");
        return 1;
    }
    if (s_receiving == 0U)
    {
        ota_reply(conn, 409, "Conflict", "no receiving session\n");
        return 1;
    }
    if (is_chunk)
    {
        uint32_t offset;
        if (app_http_header_u32(request, "X-Offset", &offset) != 0 || request->body_length == 0U)
        {
            ota_reply(conn, 400, "Bad Request", "offset and nonempty body required\n");
            return 1;
        }
        if (offset != app_ota_written_bytes())
        {
            snprintf(text, sizeof(text), "offset mismatch expect=%lu\n", (unsigned long)app_ota_written_bytes());
            ota_reply(conn, 409, "Conflict", text);
            return 1;
        }
        result = app_ota_write(request->body, request->body_length);
        if (result != 0)
        {
            app_http_ota_receive_failed();
            snprintf(text, sizeof(text), "write failed rc=%d\n", result);
            ota_reply(conn, 500, "Internal Server Error", text);
            return 1;
        }
        s_activity = HAL_GetTick();
        snprintf(text, sizeof(text), "written=%lu\n", (unsigned long)app_ota_written_bytes());
        if (app_http_reply(conn, 200, "OK", "text/plain; charset=utf-8", text) != 0)
            app_http_ota_receive_failed();
        return 1;
    }
    result = app_ota_finish();
    s_receiving = 0U;
    if (result != 0)
    {
        app_ota_abort();
        snprintf(text, sizeof(text), "finish failed rc=%d\n", result);
        ota_reply(conn, 500, "Internal Server Error", text);
        return 1;
    }
    ota_reply(conn, 200, "OK", "OTA OK, rebooting\n");
    (void)netconn_close(conn);
    osDelay(400U);
    HAL_NVIC_SystemReset();
    return 1;
}
