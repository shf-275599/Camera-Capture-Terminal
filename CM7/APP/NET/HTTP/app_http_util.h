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

void app_http_request_init(app_http_request_t *request);
/* 0=需要更多数据，1=请求完整，负值为应返回的 HTTP 状态码。 */
int app_http_request_feed(app_http_request_t *request, const uint8_t *data, size_t length);
/* 0=找到，1=缺失，-1=重复或格式非法。 */
int app_http_header_get(const app_http_request_t *request, const char *name, char *value, size_t size);
int app_http_header_u32(const app_http_request_t *request, const char *name, uint32_t *value);
int app_http_reply(struct netconn *conn, int code, const char *reason, const char *type, const char *body);
int app_http_send_all(struct netconn *conn, const void *data, size_t length);

#endif