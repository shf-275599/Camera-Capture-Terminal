/**
 * @file    app_http_util.c
 * @brief   读取请求、处理 TCP 分包、解析方法和路径、发送响应
 */
#include "app_http_util.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief   比较两个字符串是否相等，忽略大小写
 * @param   a  第一个字符串
 * @param   length  字符串长度
 * @param   b  第二个字符串
 * @retval  0 不相等；1 相等
 */
static int http_name_equal(const char *a, size_t length, const char *b)
{
    if (strlen(b) != length)
    {
        return 0;
    }
    for (size_t i = 0U; i < length; i++)
    {
        char x = a[i];
        char y = b[i];
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return 0;
    }
    return 1;
}

/**
 * @brief   从 HTTP 请求头中获取指定名称的值
 * @param   request  已解析完成的 HTTP 请求及完整请求体
 * @param   name  要获取的头字段名称
 * @param   value  存储获取到的值的缓冲区
 * @param   size  缓冲区大小
 * @retval  0 成功；-1 失败
 */
int app_http_header_get(const app_http_request_t *request, const char *name, char *value, size_t size)
{
    const char *line = strstr(request->header, "\r\n");
    int found = 0;
    if (line == NULL || size == 0U) return -1;
    line += 2;
    while (*line != '\r' && *line != '\0')
    {
        const char *end = strstr(line, "\r\n");
        const char *colon = strchr(line, ':');
        if (end == NULL || colon == NULL || colon >= end) return -1;
        if (http_name_equal(line, (size_t)(colon - line), name))
        {
            const char *start = colon + 1;
            const char *last = end;
            if (found != 0) return -1;
            while (start < end && (*start == ' ' || *start == '\t')) start++;
            while (last > start && (last[-1] == ' ' || last[-1] == '\t')) last--;
            size_t length = (size_t)(last - start);
            if (length >= size) return -1;
            memcpy(value, start, length);
            value[length] = '\0';
            found = 1;
        }
        line = end + 2;
    }
    return found ? 0 : 1;
}

/**
 * @brief   从 HTTP 请求头中获取指定名称的值，将其转换为无符号 32 位整数
 * @param   request  已解析完成的 HTTP 请求及完整请求体
 * @param   name  要获取的头字段名称
 * @param   value  存储获取到的值的指针
 * @retval  0 成功；-1 失败
 */
int app_http_header_u32(const app_http_request_t *request, const char *name, uint32_t *value)
{
    char text[24];
    int result = app_http_header_get(request, name, text, sizeof(text));
    const char *p = text;
    uint32_t number = 0U;
    uint32_t base = 10U;
    if (result != 0) return result;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
    {
        base = 16U;
        p += 2;
    }
    if (*p == '\0') return -1;
    while (*p != '\0')
    {
        uint32_t digit;
        if (*p >= '0' && *p <= '9') digit = (uint32_t)(*p - '0');
        else if (*p >= 'a' && *p <= 'f') digit = (uint32_t)(*p - 'a') + 10U;
        else if (*p >= 'A' && *p <= 'F') digit = (uint32_t)(*p - 'A') + 10U;
        else return -1;
        if (digit >= base || number > (UINT32_MAX - digit) / base) return -1;
        number = number * base + digit;
        p++;
    }
    *value = number;
    return 0;
}

/**
 * @brief   初始化 HTTP 请求结构体
 * @param   request  要初始化的 HTTP 请求结构体指针
 */
void app_http_request_init(app_http_request_t *request)
{
    memset(request, 0, sizeof(*request));
}

/**
 * @brief   解析 HTTP 请求头
 * @param   request  要解析的 HTTP 请求结构体指针
 * @retval  0 成功；-1 失败
 */
static int http_parse_header(app_http_request_t *request)
{
    char version[16];
    char extra;
    char value[64];
    const char *line_end = strstr(request->header, "\r\n");
    const char *line;
    int result;
    if (line_end == NULL) return -400;
    /* 限定请求行，避免 sscanf 越过 CRLF 读取头字段。 */
    char first_line[128];
    size_t length = (size_t)(line_end - request->header);
    if (length >= sizeof(first_line)) return -414;
    memcpy(first_line, request->header, length);
    first_line[length] = '\0';
    if (sscanf(first_line, "%7s %63s %15s %c", request->method, request->path, version, &extra) != 3 ||
        (strcmp(version, "HTTP/1.1") != 0 && strcmp(version, "HTTP/1.0") != 0) ||
        request->path[0] != '/') return -400;
    char *query = strchr(request->path, '?');
    if (query != NULL) *query = '\0';
    for (line = line_end + 2; *line != '\r'; )
    {
        const char *end = strstr(line, "\r\n");
        const char *colon = strchr(line, ':');
        if (end == NULL || colon == NULL || colon == line || colon >= end) return -400;
        for (const char *p = line; p < colon; p++)
        {
            if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                  (*p >= '0' && *p <= '9') || *p == '-')) return -400;
        }
        line = end + 2;
    }
    result = app_http_header_get(request, "Transfer-Encoding", value, sizeof(value));
    if (result != 1) return -400;
    result = app_http_header_get(request, "Expect", value, sizeof(value));
    if (result != 1) return -417;
    result = app_http_header_u32(request, "Content-Length", &request->content_length);
    if (result < 0) return -400;
    request->has_content_length = (result == 0);
    if (strcmp(request->method, "POST") == 0 && result == 1) return -411;
    if (request->content_length > APP_HTTP_BODY_MAX) return -413;
    return 0;
}

/**
 * @brief   向 HTTP 请求结构体中添加数据
 * @param   request  要添加数据的 HTTP 请求结构体指针
 * @param   data  要添加的数据指针
 * @param   length  要添加的数据长度
 * @retval  0 成功；-1 失败
 */
int app_http_request_feed(app_http_request_t *request, const uint8_t *data, size_t length)
{
    for (size_t i = 0U; i < length; i++)
    {
        if (request->header_complete == 0U)
        {
            if (request->header_length >= APP_HTTP_HEADER_MAX) return -431;
            if (data[i] == 0U || (data[i] < 32U && data[i] != '\r' && data[i] != '\n' && data[i] != '\t')) return -400;
            request->header[request->header_length++] = (char)data[i];
            request->header[request->header_length] = '\0';
            if (request->header_length >= 4U &&
                memcmp(request->header + request->header_length - 4U, "\r\n\r\n", 4U) == 0)
            {
                int result = http_parse_header(request);
                if (result != 0) return result;
                request->header_complete = 1U;
            }
        }
        else
        {
            if (request->body_length >= request->content_length) return -400;
            request->body[request->body_length++] = data[i];
        }
    }
    return (request->header_complete && request->body_length == request->content_length) ? 1 : 0;
}

/**
 * @brief   发送 HTTP 响应
 * @param   conn  客户端连接
 * @param   code  HTTP 劶态码
 * @param   reason  响应原因
 * @param   type  响应内容类型
 * @param   body  响应体指针
 * @retval  0 成功；-1 失败
 */
int app_http_reply(struct netconn *conn, int code, const char *reason, const char *type, const char *body)
{
    char header[256];
    size_t length = strlen(body);
    int n = snprintf(header, sizeof(header),
                     "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n"
                     "Connection: close\r\nCache-Control: no-store\r\n\r\n",
                     code, reason, type, (unsigned long)length);
    if (n <= 0 || (size_t)n >= sizeof(header)) return -1;
    if (app_http_send_all(conn, header, (size_t)n) != 0) return -1;
    return app_http_send_all(conn, body, length);
}

/**
 * @brief   发送 HTTP 响应体
 * @param   conn  客户端连接
 * @param   data  要发送的数据指针
 * @param   length  要发送的数据长度
 * @retval  0 成功；-1 失败
 */
int app_http_send_all(struct netconn *conn, const void *data, size_t length)
{
    const uint8_t *bytes = data;
    while (length != 0U)
    {
        size_t written = 0U;
        if (netconn_write_partly(conn, bytes, length, NETCONN_COPY, &written) != ERR_OK ||
            written == 0U || written > length) return -1;
        bytes += written;
        length -= written;
    }
    return 0;
}
