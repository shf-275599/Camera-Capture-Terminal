#include "app_ipc_cm4.h"
#include "openamp.h"

static struct rpmsg_endpoint s_ept;

static int cm4_rx_callback(struct rpmsg_endpoint *rp_chnl, 
                       void *data, 
                       size_t len, 
                       uint32_t src, 
                       void *priv)
{
  char msg[64];
  memset(msg, 0, sizeof(msg));

  (void)src;
  (void)priv;

  if (len >=sizeof(msg))
  {
    len = sizeof(msg) - 1;
  }

  memcpy(msg, data, len);
  msg[len] = '\0';

  printf("CM4: Received message: %s\n", msg);

  if (is_rpmsg_ept_ready(rp_chnl))
  {
    const char reply[] = "Hello, from CM4!";
    rpmsg_send(rp_chnl, reply, strlen(reply));
  }

  return RPMSG_SUCCESS;
}

void app_ipc_cm4_init(void)
{
  printf("CM4: OpenAMP initializing\n");
  OPENAMP_init_ept(&s_ept);
  printf("CM4: OPENAMP_init_ept OK\n");
  if (MX_OPENAMP_Init(RPMSG_REMOTE, NULL) != 0)
  {
    printf("CM4: MX_OPENAMP_Init failed\n");
    Error_Handler();
  }
  printf("CM4: MX_OPENAMP_Init OK\n");
  if (OPENAMP_create_endpoint(&s_ept,
                              RPMSG_SERVICE_NAME,
                              RPMSG_ADDR_ANY,
                              cm4_rx_callback,
                              NULL) != 0)
  {
    printf("CM4: OPENAMP_create_endpoint failed\n");
    Error_Handler();
  }
  printf("CM4: OpenAMP ready\n");
}