/**
 * @file    cm4_ota_hold.c
 * @brief   CM4 OTA 停核握手实现（CM7 / CM4 各编进本文件对应分支）
 */
#include "cm4_ota_hold.h"

#include "stm32h7xx_hal.h"
#include "boot_slot.h"

#if defined(CORE_CM7)
/**
 * @brief  CM7 读取共享运行标记并解析 CM4 核内槽号
 * @retval 0 A 槽；1 B 槽；255 标记无效
 */
uint8_t cm4_ota_running_slot(void)
{
  uint32_t value = *(volatile uint32_t *)CM4_OTA_RUNNING_ADDR;
  /* 一次读取同时获取 magic 和槽号，屏蔽最低位后验证标记。 */
  return ((value & 0xFFFFFFFEUL) == CM4_OTA_RUNNING_MAGIC) ? (uint8_t)(value & 1U) : 255U;
}
#endif

#if defined(CORE_CM7)
static volatile uint32_t *const s_req = (volatile uint32_t *)CM4_OTA_HOLD_REQ_ADDR; // CM7→CM4 停核请求字
static volatile uint32_t *const s_ack = (volatile uint32_t *)CM4_OTA_HOLD_ACK_ADDR; // CM4→CM7 已停核应答字
#endif

#if defined(CORE_CM7)

/**
 * @brief  CM7 发布 hold 请求并等待 CM4 进入 RAM 等待区
 * @param  timeout_ms  等待超时，单位 ms
 * @retval 0 收到有效 ACK；-1 ACK 残留或等待超时
 * @note   超时不清 REQ，由调用方统一调用 release 清理。
 */
int cm4_ota_hold_request(uint32_t timeout_ms)
{
  uint32_t t0;

  /* 上一轮 ACK 尚未撤销时，拒绝发起新的握手。 */
  if (*s_ack != 0U)
  {
    return -1;
  }
  *s_req = CM4_OTA_HOLD_REQ_MAGIC;
  /* 确保共享请求写入完成后再开始等待应答。 */
  __DSB();
  __DMB();

  t0 = HAL_GetTick();
  while ((HAL_GetTick() - t0) < timeout_ms)
  {
    if (*s_ack == CM4_OTA_HOLD_ACK_MAGIC)
    {
      return 0;
    }
    HAL_Delay(1);
  }

  return -1;
}

/**
 * @brief  CM7 撤销请求，并限时等待 CM4 撤销 ACK
 * @retval 无
 * @note   等待上限 3000 ms；返回时 ACK 不一定已清零。
 */
void cm4_ota_hold_release(void)
{
  uint32_t start = HAL_GetTick();
  *s_req = 0U;
  __DSB();
  __DMB();

  while ((*s_ack != 0U) && ((HAL_GetTick() - start) < 3000U))
  {
    HAL_Delay(1U);
  }
  __DSB();
}
#endif /* CORE_CM7 */

#if defined(CORE_CM4)
/**
 * @brief  CM4 将自身向量表对应的运行槽发布到共享内存
 * @retval 无
 */
void cm4_ota_publish_running_slot(void)
{
  uint8_t slot = cm4_boot_get_running_slot();
  *(volatile uint32_t *)CM4_OTA_RUNNING_ADDR =
      (slot < CM4_APP_SLOT_COUNT) ? (CM4_OTA_RUNNING_MAGIC | slot) : 0U;
  __DMB();
}
/**
 * @brief  CM4 在 RAM 中响应 hold，等待请求撤销并持续喂狗
 * @retval 1 完成 hold；0 无请求或请求已取消
 * @note   ACK 发布后不访问 Bank2；调用前应停止可能访问 Bank2 的 DMA。
 */
int __RAM_FUNC cm4_ota_hold_service(void)
{
  uint32_t primask = __get_PRIMASK();
  volatile uint32_t *req = (volatile uint32_t *)CM4_OTA_HOLD_REQ_ADDR;
  volatile uint32_t *ack = (volatile uint32_t *)CM4_OTA_HOLD_ACK_ADDR;
  if (*req != CM4_OTA_HOLD_REQ_MAGIC)
  {
    return 0;
  }
  /* ACK 后只执行 RAM 代码，不取 Bank2 指令、不运行 Flash 中的中断。 */
  __disable_irq();
  /* 关闭中断前 CM7 可能已取消请求，确认后才能发布 ACK。 */
  if (*req != CM4_OTA_HOLD_REQ_MAGIC)
  {
    __set_PRIMASK(primask);
    return 0;
  }
  *ack = CM4_OTA_HOLD_ACK_MAGIC;
  __DSB();
  while (*req == CM4_OTA_HOLD_REQ_MAGIC)
  {
    IWDG2->KR = 0xAAAAU;
  }
  *ack = 0U;
  /* 先让 CM7 观察到 ACK 撤销，再恢复进入函数前的中断状态。 */
  __DSB();
  __set_PRIMASK(primask);
  return 1;
}
#endif /* CORE_CM4 */
