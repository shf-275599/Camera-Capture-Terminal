/**
 * @file    app_http_server.c
 * @brief   创建 HTTP 任务、监听端口、接收连接、路由分发
 */
#include "app_http_server.h"
#include "app_http_util.h"
#include "app_http_ota.h"
#include "lwip/api.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip.h"
#include "boot_slot.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"
#include "mylog.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define APP_HTTP_PORT 80U              // HTTP 服务器端口
#define APP_HTTP_RECV_TIMEOUT_MS 5000U // HTTP 接收超时时间

extern struct netif gnetif; // 网络接口结构体

static app_http_request_t s_request;

static void http_handle_client(struct netconn *conn);
static void http_handle_status(struct netconn *conn);
static int http_read_request(struct netconn *conn);
static void http_reply_text(struct netconn *conn, int status_code, const char *status_text,
                            const char *content_type, const char *body);
static int http_send_all(struct netconn *conn, const void *data, size_t length);

/**
 * @brief   HTTP 服务器任务
 * @param   none
 * @return  none
 */
static void Http_Thread(void *argument)
{
    struct netconn *listen_connection; // 监听连接
    struct netconn *client_connection; // 客户端连接
    err_t error;                       // 错误码

    (void)argument; // 忽略参数

    osDelay(1000);

    /* 创建监听连接 */
    listen_connection = netconn_new(NETCONN_TCP);
    if (listen_connection == NULL)
    {
        LOG_ERROR("CM7", "HTTP: netconn_new failed");
        return;
    }

    /* 绑定端口 */
    error = netconn_bind(listen_connection, IP_ADDR_ANY, APP_HTTP_PORT);
    if (error != ERR_OK)
    {
        LOG_ERROR("CM7", "HTTP: bind port %u failed, error=%d",
                  (unsigned int)APP_HTTP_PORT, (int)error);
        netconn_delete(listen_connection); // 删除监听连接
        return;
    }

    /* 监听连接 */
    error = netconn_listen(listen_connection);
    if (error != ERR_OK)
    {
        LOG_ERROR("CM7", "HTTP: listen port %u failed, error=%d",
                  (unsigned int)APP_HTTP_PORT, (int)error);
        netconn_delete(listen_connection); // 删除监听连接
        return;
    }

    LOG_INFO("CM7", "HTTP: server listening on port %u",
              (unsigned int)APP_HTTP_PORT);
    netconn_set_recvtimeout(listen_connection, 250);

    for (;;)
    {
        client_connection = NULL;
        app_http_ota_poll();
        /* 接受连接 */
        error = netconn_accept(listen_connection, &client_connection);
        if (error != ERR_OK)
        {
            osDelay(100U);
            continue;
        }
        http_handle_client(client_connection); // 处理客户端请求
        netconn_close(client_connection);      // 关闭客户端连接
        netconn_delete(client_connection);     // 删除客户端连接
    }
}

/**
 * @brief   启动 HTTP 服务器
 * @param   none
 * @return  none
 */
void App_HTTP_Server_Thread_Start(void)
{
    BaseType_t xReturn = pdPASS;
    xReturn = xTaskCreate((TaskFunction_t)Http_Thread,
                          (char *)"Http_Thread",
                          (uint16_t)1024,
                          (void *)NULL,
                          (uint8_t)5,
                          (TaskHandle_t *)NULL);
    if (xReturn != pdPASS)
    {
        LOG_ERROR("CM7", "HTTP: Http_Thread creation failed");
        return;
    }
}

/**
 * @brief   处理客户端请求
 * @param   conn  客户端连接
 * @return  none
 */
static void http_handle_client(struct netconn *conn)
{
    /* 读取 HTTP 请求 */
    int result = http_read_request(conn);
    if (result != 0)
    {
        app_http_ota_receive_failed();
        http_reply_text(conn,
                        -result,
                        "Request Rejected",
                        "text/plain; charset=utf-8",
                        "bad request\r\n");
        return;
    }

    app_http_ota_poll();
    if (app_http_ota_dispatch(conn, &s_request) != 0) return;
    if (strcmp(s_request.method, "GET") != 0)
    {
        /* 提取路径失败，回复 405 错误码 */
        http_reply_text(conn,
                        405,
                        "Method Not Allowed",
                        "text/plain; charset=utf-8",
                        "only GET is supported\r\n");
        return;
    }

    /* 处理 /status 请求 */
    if (strcmp(s_request.path, "/status") == 0 && s_request.content_length == 0U)
    {
        /* 处理 /status 请求 */
        http_handle_status(conn);
        return;
    }

    http_reply_text(conn,
                    404,
                    "Not Found",
                    "text/plain; charset=utf-8",
                    "not found\r\n");
}

/**
 * @brief   处理 /status 请求
 * @param   conn  客户端连接
 * @return  none
 */
static void http_handle_status(struct netconn *conn)
{
    char body[256];
    char ip_address[IP4ADDR_STRLEN_MAX];
    uint8_t running_slot;
    int body_length;

    /* 获取 IP 地址 */
    ip4addr_ntoa_r(netif_ip4_addr(&gnetif), ip_address, sizeof(ip_address));
    /* 获取运行槽位 */
    running_slot = boot_get_running_slot();
    /* 构建响应体，包含设备信息、网络状态、IP 地址和 CM7 运行槽位 */
    body_length = snprintf(body,
                           sizeof(body),
                           "{"
                           "\"device\":\"STM32H757\","
                           "\"network\":\"%s\","
                           "\"ip\":\"%s\","
                           "\"cm7_running_slot\":%u"
                           "}\r\n",
                           (netif_is_link_up(&gnetif) &&
                            netif_is_up(&gnetif))
                               ? "up"
                               : "down",
                           ip_address,
                           (unsigned int)running_slot);

    /* 检查响应体长度是否有效 */
    if ((body_length <= 0) || ((size_t)body_length >= sizeof(body)))
    {
        /* 响应体长度无效，回复 500 错误码 */
        http_reply_text(conn,
                        500,
                        "Internal Server Error",
                        "text/plain; charset=utf-8",
                        "status response too large\r\n");
        return;
    }
    /* 响应体长度有效，回复 200 成功码 */
    http_reply_text(conn,
                    200,
                    "OK",
                    "application/json; charset=utf-8",
                    body);
}

/**
 * @brief   获取请求路径
 * @param   request  HTTP 请求字符串
 * @param   path     存储路径的缓冲区
 * @param   path_size  路径缓冲区大小
 * @return  none
 */
/**
 * @brief   读取客户端请求
 * @param   conn  客户端连接
 * @param   buffer  存储请求的缓冲区
 * @param   buffer_size  缓冲区大小
 * @return  none
 */
static int http_read_request(struct netconn *conn)
{
    struct netbuf *net_buffer = NULL;
    err_t error;
    uint32_t started = HAL_GetTick();
    int result = 0;
    app_http_request_init(&s_request);

    /* 设置接收超时 */
    netconn_set_recvtimeout(conn, 1000);
    netconn_set_sendtimeout(conn, APP_HTTP_RECV_TIMEOUT_MS);

    while (result == 0)
    {
        void *data;
        u16_t data_length;

        /* 接收数据 */
        error = netconn_recv(conn, &net_buffer);
        if (error != ERR_OK)
        {
            app_http_ota_poll();
            if (error == ERR_TIMEOUT && HAL_GetTick() - started < 30000U) continue;
            return error == ERR_TIMEOUT ? -408 : -400;
        }

        do
        {
            data = NULL;
            data_length = 0U;

            /* 处理数据 */
            if (netbuf_data(net_buffer, &data, &data_length) == ERR_OK)
            {
                result = app_http_request_feed(&s_request, data, data_length);
            }

            if (result < 0) break;
        } while (netbuf_next(net_buffer) >= 0); // 处理下一个数据块

        netbuf_delete(net_buffer); // 释放接收缓冲区
        net_buffer = NULL;         // 重置指针
        if (HAL_GetTick() - started >= 30000U && result == 0) return -408;
    }

    return result == 1 ? 0 : result;
}

/**
 * @brief   回复文本内容
 * @param   conn  客户端连接
 * @param   status_code  状态码
 * @param   status_text  状态文本
 * @param   content_type  内容类型
 * @param   body  回复内容
 * @return  none
 */
static void http_reply_text(struct netconn *conn,
                            int status_code,
                            const char *status_text,
                            const char *content_type,
                            const char *body)
{
    char header[256];   // HTTP 响应头缓冲区
    int header_length;  // HTTP 响应头长度
    size_t body_length; // 回复内容长度

    body_length = strlen(body); // 计算回复内容长度
    /* 构建 HTTP 响应头，包含状态码、状态文本、内容类型、内容长度、连接类型、缓存控制指令 */
    header_length = snprintf(header,
                             sizeof(header),
                             "HTTP/1.1 %d %s\r\n"
                             "Content-Type: %s\r\n"
                             "Content-Length: %lu\r\n"
                             "Connection: close\r\n"
                             "Cache-Control: no-store\r\n"
                             "\r\n",
                             status_code,
                             status_text,
                             content_type,
                             (unsigned long)body_length);

    /* 检查 HTTP 响应头是否有效 */
    if ((header_length <= 0) || ((size_t)header_length >= sizeof(header)))
    {
        return;
    }

    /* 发送 HTTP 响应头 */
    if (http_send_all(conn, header, (size_t)header_length) != 0)
    {
        return;
    }

    /* 发送回复内容 */
    (void)http_send_all(conn, body, body_length);
}

/**
 * @brief   发送所有数据
 * @param   conn  客户端连接
 * @param   data  要发送的数据
 * @param   length  数据长度
 * @return  none
 */
static int http_send_all(struct netconn *conn, const void *data, size_t length)
{
    /* 发送数据 */
    if (app_http_send_all(conn, data, length) != 0)
    {
        return -1;
    }

    return 0;
}
