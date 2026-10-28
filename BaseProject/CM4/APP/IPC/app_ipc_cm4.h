#ifndef __APP_IPC_CM4_H__
#define __APP_IPC_CM4_H__

#define RPMSG_SERVICE_NAME "IPC"

void App_IPC_Thread_Start(void);
void app_ipc_cm4_init(void);
void app_ipc_cm4_send(void);

#endif
