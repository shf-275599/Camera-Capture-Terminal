#include "app_ipc_cm7.h"
#include "openamp.h"
#include "main.h"

static struct rpmsg_endpoint s_ept;

static int cm7_rx_callback(struct rpmsg_endpoint *ept,
                           void *data,
                           size_t len,
                           uint32_t src,
                           void *priv)
{
  char message[64];

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
    if (send_len > 128)
    {
      send_len = 128;
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
  OPENAMP_Wait_EndPointready(&s_ept);
  printf("CM7: OpenAMP have been initialized\r\n");
}

void app_ipc_cm7_send(void)
{
  OPENAMP_Wait_EndPointready(&s_ept);
  const char message[] = "hello from CM7";
  if (rpmsg_send(&s_ept, message, sizeof(message)) != 0)
  {
    Error_Handler();
  }
}
