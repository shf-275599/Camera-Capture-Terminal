#ifndef HTTP_TEST_LWIP_API_H
#define HTTP_TEST_LWIP_API_H
#include <stddef.h>
struct netconn { int unused; };
#define ERR_OK 0
#define NETCONN_COPY 1
static inline int netconn_write_partly(struct netconn *conn, const void *data, size_t length, int flags, size_t *written)
{
    (void)conn; (void)data; (void)length; (void)flags;
    *written = length;
    return ERR_OK;
}
#endif
