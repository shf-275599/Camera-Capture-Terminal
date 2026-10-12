/**
 * @file    app_http_server.c
 * @brief   创建 HTTP 任务、监听端口、接收连接、路由分发
 */
#include "app_http_server.h"
#include "FreeRTOS.h"
#include "task.h"
#include "mylog.h"

static void Http_Thread(void *argument)
{

}

void App_HTTP_Server_Thread_Start(void)
{
    BaseType_t xReturn = pdPASS;
    xReturn = xTaskCreate((TaskFunction_t)Http_Thread,
                          (char *)"Http_Thread",
                          (uint16_t)4096U,
                          (void *)NULL,
                          (uint8_t)5,
                          (TaskHandle_t *)NULL);
    if (xReturn != pdPASS)
    {
        LOG_ERROR("CM7","Http_Thread creation failed");
        return;
    }
}

static void AppHttp_Thread(void *argument)
{
    LOG_INFO("CM7","Http_Thread started");
}

