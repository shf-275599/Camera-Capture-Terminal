#include "app_ipc_cm7.h"
#include "openamp.h"
#include "mylog.h"
#include "main.h"
#include "freertos.h"
#include "task.h"
#include "cmsis_os2.h"

static struct rpmsg_endpoint s_ept;

/**
 * @brief  IPC 任务
 * @note   该任务会在 FreeRTOS 中运行，用于运行 IPC 功能
 */
static void IPC_Thread(void *argument)
{
  for (;;)
  {
    OPENAMP_check_for_message();
    osDelay(100);
  }
}

/**
 * @brief  启动 IPC 任务
 */
void App_IPC_Thread_Start(void)
{
    BaseType_t xReturn = pdPASS;
      xReturn = xTaskCreate((TaskFunction_t)IPC_Thread,
                        (char *)"IPC_Thread",
                        (uint16_t)256,
                        (void *)NULL,
                        (uint8_t)5,
                        (TaskHandle_t *)NULL);
  if (xReturn != pdPASS)
  {
    return;
  }
}

/**
 * @brief  IPC 接收回调函数
 * @note   该函数会在 OpenAMP 接收消息时被调用
 */
static int cm7_rx_callback(struct rpmsg_endpoint *rp_chnl,
                           void *data,
                           size_t len,
                           uint32_t src,
                           void *priv)
{
  char msg[64];

  memset(msg, 0, sizeof(msg));

  (void)src;
  (void)priv;

  if (len >= sizeof(msg))
  {
    len = sizeof(msg) - 1;
  }

  memcpy(msg, data, len);
  msg[len] = '\0';

  printf("%s", msg);

  return RPMSG_SUCCESS;
}

/**
 * @brief  初始化 IPC，创建 OpenAMP 端点
 */
void app_ipc_cm7_init(void)
{
  OPENAMP_init_ept(&s_ept);
  if (MX_OPENAMP_Init(RPMSG_MASTER, NULL) != 0)
  {
    Error_Handler();
  }
  if (OPENAMP_create_endpoint(&s_ept,
                              RPMSG_SERVICE_NAME,
                              RPMSG_ADDR_ANY,
                              cm7_rx_callback,
                              NULL) != 0)
  {
    Error_Handler();
  }
  LOG_INFO("CM7","OpenAMP init OK");
}


