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
extern "C"
{
#endif

/** D3：CM7→CM4 停核请求字 */
#define CM4_OTA_HOLD_REQ_ADDR (0x3800F018UL)
/** D3：CM4→CM7 已停核应答字 */
#define CM4_OTA_HOLD_ACK_ADDR (0x3800F01CUL)
/** CM4 发布的运行标记：高 31 位为 magic，最低位为核内槽号（A=0、B=1） */
#define CM4_OTA_RUNNING_ADDR (0x3800F020UL)
#define CM4_OTA_RUNNING_MAGIC (0x434D3400UL)

#define CM4_OTA_HOLD_REQ_MAGIC (0x4F544148UL) /* 'OTAH' */
#define CM4_OTA_HOLD_ACK_MAGIC (0x4F54414BUL) /* 'OTAK' */

    /**
     * @brief  CM7：请求 CM4 进入 RAM 等待，并等待 ACK
     * @param  timeout_ms  等待 ACK 的超时时间，单位 ms
     * @retval 0 已收到 ACK；-1 ACK 未清理或等待超时
     * @note   失败后仍须调用 release 清理请求，防止 CM4 延迟响应后一直等待。
     */
    int cm4_ota_hold_request(uint32_t timeout_ms);

    /**
     * @brief  CM7：取消 hold（清 REQ），由 CM4 清 ACK 并恢复
     * @retval 无
     * @note   最多等待 ACK 清零 3000 ms，超时也会返回，不代表 CM4 已恢复。
     * @note   供 OTA abort 使用；finish 后走复位则不必调用
     */
    void cm4_ota_hold_release(void);

    /**
     * @brief  CM4：在 RAM 中关中断、写 ACK 并等待 REQ 清除
     * @retval 1 完成 hold 并恢复原中断状态；0 无请求或请求已取消
     * @note   须在 CM4 业务循环中周期性调用；.RamFunc 必须已复制到可执行 RAM。
     */
    int cm4_ota_hold_service(void);

    /**
     * @brief  CM4：发布自身实际运行槽，供 CM7 选择升级目标
     * @retval 无
     * @note   启动任务中调用；槽号无效时写入 0，表示尚无有效运行标记。
     */
    void cm4_ota_publish_running_slot(void);

    /**
     * @brief  CM7：读取 CM4 发布的实际运行槽
     * @retval 0 A 槽；1 B 槽；255 运行标记无效或尚未发布
     */
    uint8_t cm4_ota_running_slot(void);

#ifdef __cplusplus
}
#endif

#endif /* CM4_OTA_HOLD_H */
