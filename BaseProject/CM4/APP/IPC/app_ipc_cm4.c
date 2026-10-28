#include "app_ipc_cm4.h"
#include "openamp.h"
#include "main.h"
#include "mylog.h"
#include "freertos.h"
#include "task.h"
#include "cmsis_os2.h"
#include "cm4_ota_hold.h"

static struct rpmsg_endpoint s_ept;

static void IPC_Thread(void *argument)
{
  (void)argument;
  cm4_ota_publish_running_slot();

  for (;;)
  {
    (void)cm4_ota_hold_service();
    OPENAMP_check_for_message();
    osDelay(100);
  }
}

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

static int cm4_rx_callback(struct rpmsg_endpoint *ept,
                           void *data,
                           size_t len,
                           uint32_t src,
                           void *priv)
{
  // char message[64];
  (void)ept;
  (void)src;
  (void)priv;

  return RPMSG_SUCCESS;
}

/*
 * 覆盖 syscalls.c 中的弱定义，将 CM7 的 printf 发送到 CM4。
 */
int _write(int file, char *ptr, int len)
{
  (void)file;

  if (!is_rpmsg_ept_ready(&s_ept))
  {
    /*
     * OpenAMP 尚未建立 endpoint 时，不能等待。
     * 否则启动阶段的 printf 可能永久阻塞。
     */
    return len;
  }

  while (len > 0)
  {
    int send_len = len;

    /*
     * RPMsg 单次发送长度不能无限大。
     * 使用 128 字节可以避免超过当前 OpenAMP buffer 的有效载荷大小。
     */
    if (send_len > 256)
    {
      send_len = 256;
    }

    if (rpmsg_send(&s_ept, ptr, (size_t)send_len) != 0)
    {
      return -1;
    }

    ptr += send_len;
    len -= send_len;
  }

  return 0;
}

void app_ipc_cm4_init(void)
{
  OPENAMP_init_ept(&s_ept);
  if (MX_OPENAMP_Init(RPMSG_REMOTE, NULL) != 0)
  {
    Error_Handler();
  }
  if (OPENAMP_create_endpoint(&s_ept,
                              RPMSG_SERVICE_NAME,
                              RPMSG_ADDR_ANY,
                              cm4_rx_callback,
                              NULL) != 0)
  {
    Error_Handler();
  }
  OPENAMP_Wait_EndPointready(&s_ept);
  LOG_INFO("CM4-IPC", "OpenAMP init OK");
}

void app_ipc_cm4_send(void)
{
  OPENAMP_Wait_EndPointready(&s_ept);
  const char message[] = "hello from CM4";
  if (rpmsg_send(&s_ept, message, sizeof(message)) != 0)
  {
    Error_Handler();
  }
}
