#ifndef APP_STORAGE_H
#define APP_STORAGE_H
typedef enum { STORAGE_LOCAL, STORAGE_SWITCHING, STORAGE_USB, STORAGE_FAULT } storage_mode_t;
/** @brief 获取本地、切换中、USB 导出或故障模式；不代表已取得访问权。 */
storage_mode_t app_storage_mode(void);
/** @brief 初始化并挂载本地存储、启动 USB 任务；由抓拍任务调用一次。 */
void app_storage_start(void);
/** @brief 尝试取得抓拍使用权，接受返回 1，否则返回 0；成功后须调用 snapshot_end。 */
int app_storage_snapshot_begin(void);
/** @brief 释放抓拍使用权；clean 为 0 表示文件关闭失败，进入需复位恢复的故障状态。 */
void app_storage_snapshot_end(int clean);
/** @brief 提交切换请求，接受返回 1，否则返回 0；实际切换由 service 执行。 */
int app_storage_toggle(void);
/** @brief 处理延迟切换；处理过请求返回 1，无操作返回 0，成功与否以当前模式为准。 */
int app_storage_service(void);
/** @brief 尝试阻止抓拍与模式切换，成功返回 0，拒绝返回 -1；成功后须调用 ota_unlock。 */
int app_storage_ota_lock(void);
/** @brief 释放 OTA 存储占用。 */
void app_storage_ota_unlock(void);
#endif
