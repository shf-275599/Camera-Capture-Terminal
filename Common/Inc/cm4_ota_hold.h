/**
 * @file    cm4_ota_hold.h
 * @brief   CM4 OTA 前停核握手（D3 SRAM mailbox）
 *
 * 流程：
 *   1) CM7 写 REQ magic → 2) CM4 在 RAM 中关中断并写 ACK →
 *   3) CM7 擦写 Bank2 空闲槽 → 4) abort 时清 REQ，CM4 恢复；
 *      finish 成功则系统复位，无需再唤醒。
 *
 * @note  CM4 从 Bank2 取指，擦写同 Bank 前必须停核；Stub(S0) 永不擦。
 * @note  mailbox 地址紧挨 cm4_boot_handoff（0x3800F010/014）。
 */
#ifndef CM4_OTA_HOLD_H
#define CM4_OTA_HOLD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** D3：CM7→CM4 停核请求字 */
#define CM4_OTA_HOLD_REQ_ADDR   (0x3800F018UL)
/** D3：CM4→CM7 已停核应答字 */
#define CM4_OTA_HOLD_ACK_ADDR   (0x3800F01CUL)
#define CM4_OTA_RUNNING_ADDR   (0x3800F020UL)
#define CM4_OTA_RUNNING_MAGIC  (0x434D3400UL)

#define CM4_OTA_HOLD_REQ_MAGIC  (0x4F544148UL) /* 'OTAH' */
#define CM4_OTA_HOLD_ACK_MAGIC  (0x4F54414BUL) /* 'OTAK' */

/**
 * @brief  CM7：请求 CM4 进入 RAM 等待，并等待 ACK
 * @param  timeout_ms  最长等待毫秒
 * @retval 0 已收到 ACK；-1 超时
 */
int cm4_ota_hold_request(uint32_t timeout_ms);

/**
 * @brief  CM7：取消 hold（清 REQ），由 CM4 清 ACK 并恢复
 * @note   供 OTA abort 使用；finish 后走复位则不必调用
 */
void cm4_ota_hold_release(void);

/**
 * @brief  CM4：在 RAM 中关中断、写 ACK 并等待 REQ 清除
 * @retval 1 经历了一次 hold 并已唤醒；0 当前无请求
 * @note   须在 CM4 业务循环中周期性调用（如 ipcTask）
 */
int cm4_ota_hold_service(void);
void cm4_ota_publish_running_slot(void);
uint8_t cm4_ota_running_slot(void);

#ifdef __cplusplus
}
#endif

#endif /* CM4_OTA_HOLD_H */
