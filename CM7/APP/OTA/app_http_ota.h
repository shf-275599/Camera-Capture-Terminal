/**
 * @file    app_http_ota.h
 * @brief   HTTP OTA 路由、会话超时和接收失败清理接口
 * @note    所有接口由同一 HTTP 任务串行调用。
 */
#ifndef APP_HTTP_OTA_H
#define APP_HTTP_OTA_H

#include "app_http_util.h"

/**
 * @brief  分发完整 HTTP 请求到对应 OTA 接口并发送响应
 * @param  conn     当前客户端连接
 * @param  request  已解析完成的请求，包含完整请求体
 * @retval 0 非 OTA 路径，交给其他路由；1 已处理，包含错误响应
 * @note   end 成功后关闭连接并复位；调用方须保证参数有效。
 */
int app_http_ota_dispatch(struct netconn *conn, const app_http_request_t *request);

/**
 * @brief  检查 CM4 启动提交条件，并清理超时的接收会话
 * @retval 无
 * @note   HTTP 任务等待连接或接收数据期间也须周期调用。
 */
void app_http_ota_poll(void);

/**
 * @brief  接收或响应发送失败时，中止尚未完成的接收会话
 * @retval 无
 * @note   恢复资源由 app_ota_abort 完成；无接收会话时不执行操作。
 */
void app_http_ota_receive_failed(void);

#endif
