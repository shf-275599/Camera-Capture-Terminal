/**
 * @file  app_http_ota.c
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

#define HTTP_OTA_IDLE_MS 60000U   // begin 成功或上次成功写入后的空闲上限
#define HTTP_OTA_TOTAL_MS 600000U // begin 成功后的会话总时长上限

static uint8_t s_receiving;        // 是否正在接收数据
static uint32_t s_started;         // 会话开始时间
static uint32_t s_activity;        // begin 成功或最后成功写入的时间
static uint8_t s_cm4_boot_checked; // 是否已检查 CM4 启动槽位

/**
 * @brief 回复 HTTP 请求
 * @param conn  客户端连接
 * @param code  HTTP 状态码
 * @param reason  状态码描述
 * @param body  以 '\0' 结尾的响应体字符串
 * @retval 无
 * @note  不更新会话活动时间；需要处理发送失败的路径直接调用 app_http_reply。
 */
static void ota_reply(struct netconn *conn, int code, const char *reason, const char *body)
{
    (void)app_http_reply(conn, code, reason, "text/plain; charset=utf-8", body);
}

/**
 * @brief 连接失败时保留接收会话，等待客户端重连或会话超时
 * @retval 无
 * @note  未完整接收的请求体尚未交给 app_ota_write，不推进偏移或刷新计时。
 */
void app_http_ota_receive_failed(void)
{
    if (s_receiving != 0U) // 如果正在接收数据
    {
        LOG_WARN("HTTP-OTA", "connection failed, session kept written=%lu",
                 (unsigned long)app_ota_written_bytes());
    }
}

/**
 * @brief 处理 CM4 启动确认和 OTA 接收会话超时
 * @retval 无
 * @note  由 HTTP 任务反复调用，与 OTA 请求串行执行：
 *        1. 无升级会话时，确认 CM4 实际运行的 pending 槽；EEPROM 失败则后续重试。
 *        2. 接收会话空闲达 60 秒或总时长达 10 分钟时，取消升级并释放资源。
 *        本函数不接收固件数据，也不因轮询或状态查询延长会话时限。
 */
void app_http_ota_poll(void)
{
    uint32_t now = HAL_GetTick();
    /* CM4 任务上报实际运行槽后，由同一管理任务确认本轮启动。 */
    uint8_t running = cm4_ota_running_slot();
    /* 无升级会话时，确认 CM4 实际运行的 pending 槽；EEPROM 失败则后续重试。 */
    if (s_cm4_boot_checked == 0U && running < CM4_APP_SLOT_COUNT && !app_ota_is_busy())
    {
        boot_eeprom_meta_t meta;
        if (boot_eeprom_load(&meta) == 0)
        {
            /* 无待确认镜像时结束检查；有 pending 时只确认实际运行的候选槽。 */
            if ((meta.cm4_flags & BOOT_EE_FLAG_CM4_PENDING) == 0U)
                s_cm4_boot_checked = 1U; // 无 pending 时直接确认
            /* 确认实际运行槽位的 pending 时，提交并更新状态。 */
            else if (meta.cm4_pending_slot == running && boot_eeprom_commit_cm4_pending(&meta) == 0)
            {
                s_cm4_boot_checked = 1U; // 确认 pending 时更新状态
                LOG_INFO("HTTP-OTA", "CM4 pending committed -> confirmed=%u", running);
            }
        }
    }
    /* 会话超时处理 */
    if (s_receiving != 0U && ((now - s_activity >= HTTP_OTA_IDLE_MS) || (now - s_started >= HTTP_OTA_TOTAL_MS)))
    {
        LOG_WARN("HTTP-OTA", "session timeout total=%lu idle=%lu",
                 (unsigned long)(now - s_started), (unsigned long)(now - s_activity));
        app_ota_abort();  // 取消当前会话
        s_receiving = 0U; // 重置接收状态
    }
}

/**
 * @brief 分发 OTA 状态查询、开始、分块写入、结束和取消请求
 * @param conn  客户端连接
 * @param request  已解析完成的 HTTP 请求及完整请求体
 * @retval 0 非 OTA 路径；1 已处理，包括返回 HTTP 错误的情况
 * @note  end 成功后返回响应并复位；长度上限由 HTTP 请求解析层检查。
 */
int app_http_ota_dispatch(struct netconn *conn, const app_http_request_t *request)
{
    app_ota_core_t core = APP_OTA_CORE_CM7;                           // 默认 CM7 核心
    char text[1024];                                                  // 响应体缓冲区
    char core_name[16];                                               // 核心名称缓冲区
    int result;                                                       // 操作结果
    int is_status = strcmp(request->path, "/ota/status") == 0;        // 是否状态查询
    int is_begin = strcmp(request->path, "/ota/begin") == 0;          // 是否开始
    int is_chunk = strcmp(request->path, "/ota/chunk") == 0;          // 是否分块写入
    int is_end = strcmp(request->path, "/ota/end") == 0;              // 是否结束
    int is_abort = strcmp(request->path, "/ota/abort") == 0;          // 是否取消
    if (!is_status && !is_begin && !is_chunk && !is_end && !is_abort) // 如果不是 OTA 路径
        return 0;
    /* 仅状态查询使用 GET，其余管理接口使用 POST。 */
    if (strcmp(request->method, is_status ? "GET" : "POST") != 0)
    {
        ota_reply(conn, 405, "Method Not Allowed", "invalid method\n");
        return 1;
    }
    /* 只有 chunk 接口接收二进制请求体。 */
    if (!is_chunk && request->content_length != 0U)
    {
        ota_reply(conn, 400, "Bad Request", "body must be empty\n");
        return 1;
    }
    /* 查询和 begin 显式指定核心；后续操作沿用当前唯一会话。 */
    if (is_status || is_begin)
    {
        result = app_http_header_get(request, "X-Ota-Core", core_name, sizeof(core_name));   // 获取 HTTP 请求头字段值（核心）
        if (result != 0 || (strcmp(core_name, "cm7") != 0 && strcmp(core_name, "cm4") != 0)) // 如果获取失败或核心名称不是 cm7 或 cm4
        {
            ota_reply(conn, 400, "Bad Request", "X-Ota-Core must be cm7 or cm4\n");
            return 1;
        }
        core = strcmp(core_name, "cm4") == 0 ? APP_OTA_CORE_CM4 : APP_OTA_CORE_CM7; // 根据核心名称设置核心
    }
    if (is_status) // 如果是状态查询接口
    {
        result = app_ota_format_status(text, sizeof(text), core); // 格式化状态查询响应
        if (result < 0)
            ota_reply(conn, 503, "Service Unavailable", "running slot unavailable\n");
        else
            ota_reply(conn, 200, "OK", text);
        return 1;
    }
    if (is_begin) // 如果是开始接口
    {
        uint32_t size, version, crc, base;
        /* 获取 HTTP 请求头字段值 */
        if (app_http_header_u32(request, "X-Fw-Size", &size) != 0 ||
            app_http_header_u32(request, "X-Fw-Version", &version) != 0 ||
            app_http_header_u32(request, "X-Fw-Crc32", &crc) != 0 ||
            app_http_header_u32(request, "X-Fw-Base", &base) != 0)
        {
            ota_reply(conn, 400, "Bad Request", "size/version/crc32/base required as u32\n");
            return 1;
        }
        /* 检查是否正在忙 */
        if (app_ota_is_busy())
        {
            if (s_receiving != 0U && app_ota_session_matches(size, version, core, crc, base))
            {
                /* 重复 begin 不擦除、不重置 CRC，也不延长会话时限。 */
                snprintf(text, sizeof(text), "OTA resume written=%lu\n",
                         (unsigned long)app_ota_written_bytes());
                if (app_http_reply(conn, 200, "OK", "text/plain; charset=utf-8", text) != 0)
                    app_http_ota_receive_failed();
                return 1;
            }
            ota_reply(conn, 409, "Conflict", "OTA busy\n");
            return 1;
        }
        /* 底层完成目标槽保护、相机暂停、必要的 CM4 hold 和擦除。 */
        result = app_ota_begin(size, version, core, crc, base);
        /* 检查开始结果 */
        if (result != 0)
        {
            snprintf(text, sizeof(text), "begin failed rc=%d\n", result);
            int code = result == -2 ? 413 : (result == -8 || result == -6 ? 409 : 503);
            ota_reply(conn, code, code == 413 ? "Payload Too Large" : (code == 409 ? "Conflict" : "Service Unavailable"), text);
            return 1;
        }
        /* 擦除成功后才计时；ready 响应丢失时可重复 begin 查询偏移。 */
        s_started = s_activity = HAL_GetTick();
        s_receiving = 1U;
        /* 回复 ready 响应 */
        if (app_http_reply(conn, 200, "OK", "text/plain; charset=utf-8", "OTA ready written=0\n") != 0)
            app_http_ota_receive_failed();
        return 1;
    }
    /* 处理取消请求 */
    if (is_abort)
    {
        /* pending 已保存的阶段不可撤销，不能回复“已取消”。 */
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
    if (s_receiving == 0U) // 如果没有接收会话
    {
        ota_reply(conn, 409, "Conflict", "no receiving session\n");
        return 1;
    }
    if (is_chunk) // 如果是 chunk 接口
    {
        uint32_t offset;
        if (app_http_header_u32(request, "X-Offset", &offset) != 0 || request->body_length == 0U)
        {
            ota_reply(conn, 400, "Bad Request", "offset and nonempty body required\n");
            return 1;
        }
        /* 拒绝跳块或重复块，但保留会话供客户端查询正确偏移。 */
        if (offset != app_ota_written_bytes())
        {
            snprintf(text, sizeof(text), "offset mismatch expect=%lu\n", (unsigned long)app_ota_written_bytes());
            ota_reply(conn, 409, "Conflict", text);
            return 1;
        }
        result = app_ota_write(request->body, request->body_length);
        if (result != 0)
        {
            app_ota_abort();
            s_receiving = 0U;
            snprintf(text, sizeof(text), "write failed rc=%d\n", result);
            ota_reply(conn, 500, "Internal Server Error", text);
            return 1;
        }
        /* 仅成功接收并交给底层的数据块刷新空闲计时。 */
        s_activity = HAL_GetTick();
        snprintf(text, sizeof(text), "written=%lu\n", (unsigned long)app_ota_written_bytes());
        if (app_http_reply(conn, 200, "OK", "text/plain; charset=utf-8", text) != 0)
            app_http_ota_receive_failed();
        return 1;
    }
    /* 剩余路由为 end：底层校验完整镜像并持久化 pending。 */
    result = app_ota_finish();
    s_receiving = 0U;
    if (result != 0)
    {
        app_ota_abort();
        snprintf(text, sizeof(text), "finish failed rc=%d\n", result);
        ota_reply(conn, 500, "Internal Server Error", text);
        return 1;
    }
    /* pending 已落盘，响应丢失也继续复位，由客户端查询最终启动结果。 */
    ota_reply(conn, 200, "OK", "OTA OK, rebooting\n");
    (void)netconn_close(conn);
    osDelay(400U);
    HAL_NVIC_SystemReset();
    return 1;
}
