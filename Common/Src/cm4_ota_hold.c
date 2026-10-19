/**
 * @file    cm4_ota_hold.c
 * @brief   CM4 OTA 停核握手实现（CM7 / CM4 各编进本文件对应分支）
 */
#include "cm4_ota_hold.h"

#include "stm32h7xx_hal.h"

#if defined(CORE_CM7)
#ifndef CM4_OTA_HOLD_HSEM_ID
/** 与 OpenAMP 使用的 HSEM_ID_0 一致，用于 WFE 唤醒 */
#define CM4_OTA_HOLD_HSEM_ID  (0U)
#endif
#endif

static volatile uint32_t *const s_req =
    (volatile uint32_t *)CM4_OTA_HOLD_REQ_ADDR;
static volatile uint32_t *const s_ack =
    (volatile uint32_t *)CM4_OTA_HOLD_ACK_ADDR;

#if defined(CORE_CM7)
/**
 * @brief  置 REQ 并轮询 ACK
 * @retval 0 成功；-1 超时
 */
int cm4_ota_hold_request(uint32_t timeout_ms)
{
  uint32_t t0;

  *s_ack = 0U;
  *s_req = CM4_OTA_HOLD_REQ_MAGIC;
  __DSB();
  __DMB();

  t0 = HAL_GetTick();
  while ((HAL_GetTick() - t0) < timeout_ms) {
    if (*s_ack == CM4_OTA_HOLD_ACK_MAGIC) {
      /* 再等片刻，确保 CM4 已真正进入 STOP */
      HAL_Delay(20);
      return 0;
    }
    HAL_Delay(1);
  }

  return -1;
}

/**
 * @brief  清 REQ，HSEM take/release 产生事件唤醒 WFE
 */
void cm4_ota_hold_release(void)
{
  *s_req = 0U;
  __DSB();
  __DMB();

  /* 用 HSEM0 脉冲唤醒 STOP 中的 CM4（WFE） */
  __HAL_RCC_HSEM_CLK_ENABLE();
  if (HAL_HSEM_FastTake(CM4_OTA_HOLD_HSEM_ID) != HAL_OK) {
    HAL_HSEM_Release(CM4_OTA_HOLD_HSEM_ID, 0);
    (void)HAL_HSEM_FastTake(CM4_OTA_HOLD_HSEM_ID);
  }
  HAL_HSEM_Release(CM4_OTA_HOLD_HSEM_ID, 0);

  *s_ack = 0U;
  __DSB();
}
#endif /* CORE_CM7 */

#if defined(CORE_CM4)
/**
 * @brief  见 REQ 则 ACK 并 STOP；被唤醒后清 ACK
 * @retval 1 完成一次 hold；0 无请求
 */
int cm4_ota_hold_service(void)
{
  if (*s_req != CM4_OTA_HOLD_REQ_MAGIC) {
    return 0;
  }

  *s_ack = CM4_OTA_HOLD_ACK_MAGIC;
  __DSB();
  __DMB();

  HAL_PWREx_ClearPendingEvent();
  HAL_PWREx_EnterSTOPMode(PWR_MAINREGULATOR_ON, PWR_STOPENTRY_WFE, PWR_D2_DOMAIN);

  /* 被 CM7 abort 唤醒后清 ACK，继续业务 */
  *s_ack = 0U;
  __DSB();
  return 1;
}
#endif /* CORE_CM4 */
