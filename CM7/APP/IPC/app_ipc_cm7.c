#include "app_ipc_cm7.h"
#include "openamp.h"
#include "mylog.h"
#include "main.h"

static struct rpmsg_endpoint s_ept;

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


