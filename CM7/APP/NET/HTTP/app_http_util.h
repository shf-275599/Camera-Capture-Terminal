#ifndef __APP_HTTP_UTIL_H__
#define __APP_HTTP_UTIL_H__

#include <stddef.h>
#include <stdint.h>
#include "lwip/api.h"

#define APP_HTTP_HEADER_MAX 2048U
#define APP_HTTP_BODY_MAX 8192U

typedef struct
{
    char header[APP_HTTP_HEADER_MAX + 1U];
    uint8_t body[APP_HTTP_BODY_MAX];
    char method[8];
    char path[64];
    uint32_t content_length;
    uint32_t header_length;
    uint32_t body_length;
    uint8_t header_complete;
    uint8_t has_content_length;
} app_http_request_t;

/**
 * @brief 初始化 HTTP 请求结构体。
 * @param request HTTP 请求结构体指针。
 */
void app_http_request_init(app_http_request_t *request);

/**
 * @brief 处理 HTTP 请求数据。
 * @param request HTTP 请求结构体指针。
 * @param data 输入数据指针。
 * @param length 输入数据长度。
 * @return int 0=需要更多数据，1=请求完整，负值为应返回的 HTTP 状态码。
 */
int app_http_request_feed(app_http_request_t *request, const uint8_t *data, size_t length);

/**
 * @brief 获取 HTTP 请求头字段值。
 * @param request HTTP 请求结构体指针。
 * @param name 请求头字段名指针。
 * @param value 输出值指针。
 * @param size 输出值缓冲区大小。
 * @return int 0=找到，1=缺失，-1=重复或格式非法。
 */
int app_http_header_get(const app_http_request_t *request, const char *name, char *value, size_t size);

/**
 * @brief 获取 HTTP 请求头字段值（无符号 32 位整数）。
 * @param request HTTP 请求结构体指针。
 * @param name 请求头字段名指针。
 * @param value 输出值指针。
 * @return int 0=找到，1=缺失，-1=重复或格式非法。
 */
int app_http_header_u32(const app_http_request_t *request, const char *name, uint32_t *value);

/**
 * @brief 发送 HTTP 响应。
 * @param conn LWIP 网络连接指针。
 * @param code HTTP 状态码。
 * @param reason 状态码描述。
 * @param type 内容类型。
 * @param body 响应体指针。
 * @return int 0=成功，-1=失败。
 */
int app_http_reply(struct netconn *conn, int code, const char *reason, const char *type, const char *body);

/**
 * @brief 发送 HTTP 响应体。
 * @param conn LWIP 网络连接指针。
 * @param data 响应体指针。
 * @param length 响应体长度。
 * @return int 0=成功，-1=失败。
 */
int app_http_send_all(struct netconn *conn, const void *data, size_t length);

#endif