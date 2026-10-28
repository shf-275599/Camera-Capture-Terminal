#ifndef APP_HTTP_MJPEG_H
#define APP_HTTP_MJPEG_H

#include "lwip/api.h"

/* 成功后连接所有权移交给推流任务；失败时由 HTTP 主任务回复并释放。 */
int app_http_mjpeg_start(struct netconn *conn);
/* OTA 擦写前等待两个观看连接关闭并结束正在进行的编码。 */
int app_http_mjpeg_stop(uint32_t timeout_ms);
void app_http_mjpeg_resume(void);
/* 最近统计窗口中最快观看连接的完整帧发送速率，单位为 0.1 FPS。 */
uint32_t app_http_mjpeg_fps_x10(void);

#endif
