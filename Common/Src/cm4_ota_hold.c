/**
 * @file    cm4_ota_hold.c
 * @brief   CM4 OTA 停核握手实现（CM7 / CM4 各编进本文件对应分支）
 */
#include "cm4_ota_hold.h"

#include "stm32h7xx_hal.h"
#include "boot_slot.h"

#if defined(CORE_CM7)
uint8_t cm4_ota_running_slot(void)
{
  uint32_t value = *(volatile uint32_t *)CM4_OTA_RUNNING_ADDR;
  return ((value & 0xFFFFFFFEUL) == CM4_OTA_RUNNING_MAGIC) ? (uint8_t)(value & 1U) : 255U;
}
#endif

#if defined(CORE_CM7)
static volatile uint32_t *const s_req =
    (volatile uint32_t *)CM4_OTA_HOLD_REQ_ADDR;
static volatile uint32_t *const s_ack =
    (volatile uint32_t *)CM4_OTA_HOLD_ACK_ADDR;
#endif

#if defined(CORE_CM7)
/**
 * @brief  置 REQ 并轮询 ACK
 * @retval 0 成功；-1 超时
 */
int cm4_ota_hold_request(uint32_t timeout_ms)
{
  uint32_t t0;

  if (*s_ack != 0U)
  {
    return -1;
  }
  *s_req = CM4_OTA_HOLD_REQ_MAGIC;
  __DSB();
  __DMB();

  t0 = HAL_GetTick();
  while ((HAL_GetTick() - t0) < timeout_ms) {
    if (*s_ack == CM4_OTA_HOLD_ACK_MAGIC) {
      return 0;
    }
    HAL_Delay(1);
  }

  return -1;
}

/**
 * @brief  清 REQ，RAM 轮询中的 CM4 随后恢复
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
void cm4_ota_publish_running_slot(void)
{
  uint8_t slot = cm4_boot_get_running_slot();
  *(volatile uint32_t *)CM4_OTA_RUNNING_ADDR =
      (slot < CM4_APP_SLOT_COUNT) ? (CM4_OTA_RUNNING_MAGIC | slot) : 0U;
  __DMB();
}
/**
 * @brief  在 RAM 中关中断等待 REQ 清除，期间持续喂狗
 * @retval 1 完成一次 hold；0 无请求
 */
int __RAM_FUNC cm4_ota_hold_service(void)
{
  uint32_t primask = __get_PRIMASK();
  volatile uint32_t *req = (volatile uint32_t *)CM4_OTA_HOLD_REQ_ADDR;
  volatile uint32_t *ack = (volatile uint32_t *)CM4_OTA_HOLD_ACK_ADDR;
  if (*req != CM4_OTA_HOLD_REQ_MAGIC) {
    return 0;
  }
  /* ACK 后只执行 RAM 代码，不取 Bank2 指令、不运行 Flash 中的中断。 */
  __disable_irq();
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
  __DSB();
  __set_PRIMASK(primask);
  return 1;
}
#endif /* CORE_CM4 */
