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

  if (len >= sizeof(message))
  {
    len = sizeof(message) - 1;
  }

  memcpy(message, data, len);
  message[len] = '\0';

  printf("CM7 RX: %s\n", message);

  return RPMSG_SUCCESS;
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
}

void app_ipc_cm7_send(void)
{
  OPENAMP_Wait_EndPointready(&s_ept);
  const char message[] = "hello from CM7";
  if (rpmsg_send(&s_ept, message, sizeof(message)) != 0)
  {
    printf("CM7: rpmsg_send failed\n");
  }
}
