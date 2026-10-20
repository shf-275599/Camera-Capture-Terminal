#ifndef APP_HTTP_OTA_H
#define APP_HTTP_OTA_H

#include "app_http_util.h"

int app_http_ota_dispatch(struct netconn *conn, const app_http_request_t *request);
void app_http_ota_poll(void);
void app_http_ota_receive_failed(void);

#endif
