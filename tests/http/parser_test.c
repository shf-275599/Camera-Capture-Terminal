/** HTTP 字节流解析测试：覆盖任意 TCP 分包与恶意长度字段。 */
#include "app_http_util.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static app_http_request_t request;

static int parse(const char *text)
{
    app_http_request_init(&request);
    return app_http_request_feed(&request, (const uint8_t *)text, strlen(text));
}

int main(void)
{
    const char header[] = "POST /ota/chunk HTTP/1.1\r\nContent-Length: 4\r\nX-Offset: 0x100\r\n\r\n";
    uint8_t packet[256];
    size_t length = strlen(header);
    uint32_t number;
    memcpy(packet, header, length);
    packet[length++] = 0;
    packet[length++] = 255;
    packet[length++] = '\r';
    packet[length++] = '\n';
    for (size_t split = 1; split < length; split++)
    {
        app_http_request_init(&request);
        assert(app_http_request_feed(&request, packet, split) == 0);
        assert(app_http_request_feed(&request, packet + split, length - split) == 1);
        assert(request.body_length == 4 && request.body[0] == 0 && request.body[1] == 255);
        assert(app_http_header_u32(&request, "x-offset", &number) == 0 && number == 256);
    }
    app_http_request_init(&request);
    for (size_t i = 0; i < length; i++)
        assert(app_http_request_feed(&request, packet + i, 1) == (i + 1 == length ? 1 : 0));
    assert(parse("GET /status HTTP/1.1\r\nHost: test\r\n\r\n") == 1);
    assert(parse("POST /ota/chunk HTTP/1.1\r\n\r\n") == -411);
    assert(parse("POST /ota/chunk HTTP/1.1\r\nContent-Length: 8193\r\n\r\n") == -413);
    assert(parse("POST /ota/chunk HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\n") == -400);
    assert(parse("POST /ota/chunk HTTP/1.1\r\nContent-Length: 4294967296\r\n\r\n") == -400);
    assert(parse("POST /ota/chunk HTTP/1.1\r\nContent-Length: -1\r\n\r\n") == -400);
    assert(parse("POST /ota/chunk HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n") == -400);
    assert(parse("POST /ota/chunk HTTP/1.1\r\nExpect: 100-continue\r\n\r\n") == -417);
    assert(parse("POST /ota/chunk HTTP/1.1\r\nContent-Length: 1\r\n\r\nab") == -400);
    assert(parse("POST /ota/chunk HTTP/1.1\r\nContent-Length: 2\r\n\r\na") == 0);
    assert(parse("GET /status HTTP/1.1\r\nBad Header: value\r\n\r\n") == -400);
    app_http_request_init(&request);
    memset(packet, 'A', sizeof(packet));
    for (unsigned i = 0; i < 8; i++) assert(app_http_request_feed(&request, packet, sizeof(packet)) == 0);
    assert(app_http_request_feed(&request, packet, 1) == -431);
    puts("HTTP parser PASS: all split points, binary body, length and malformed headers");
    return 0;
}
