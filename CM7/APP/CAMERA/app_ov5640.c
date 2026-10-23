/**
 * @file    app_ov5640.c
 * @brief   OV5640摄像头简单初始化测试
 */
#include "app_ov5640.h"
#include "ov5640.h"
#include "lcd_ltdc.h"
#include "dcmi.h"
#include "ov5640_dcmi.h"
#include "cmsis_os2.h"
#include "mylog.h"
#include "freertos.h"
#include "task.h"
#include "cmsis_os2.h"

static volatile uint8_t s_pause_requested;
static volatile uint8_t s_paused;
static volatile uint8_t s_camera_running;

int app_ov5640_pause(uint32_t timeout_ms)
{
    uint32_t start = osKernelGetTickCount();
    s_pause_requested = 1U;
    while ((s_camera_running != 0U) && (s_paused == 0U))
    {
        if ((osKernelGetTickCount() - start) >=
            ((uint64_t)timeout_ms * osKernelGetTickFreq() / 1000U))
        {
            s_pause_requested = 0U;
            return -1;
        }
        osDelay(1U);
    }
    if (((DCMI->CR & DCMI_CR_CAPTURE) != 0U) ||
        ((((DMA_Stream_TypeDef *)hdcmi.DMA_Handle->Instance)->CR & DMA_SxCR_EN) != 0U) ||
        ((DMA2D->CR & DMA2D_CR_START) != 0U))
    {
        s_pause_requested = 0U;
        return -1;
    }
    return 0;
}

void app_ov5640_resume(void)
{
    s_pause_requested = 0U;
    while ((s_paused != 0U) && (s_camera_running != 0U))
    {
        osDelay(1U);
    }
}

/**
 * @brief  OV5640 摄像头任务
 * @note   该任务会在 FreeRTOS 中运行，用于运行摄像头功能
 */
static void Camera_Thread(void *argument)
{
  (void)argument;

  s_camera_running = 1U;
  if (app_ov5640_run() != 0U)
  {
    LOG_ERROR("CM7", "CAMERA_THREAD_FAIL");
  }

  s_camera_running = 0U;
  vTaskDelete(NULL);
}

/**
 * @brief  启动 OV5640 摄像头任务
 * @note   该任务会在 FreeRTOS 中运行，用于测试摄像头功能
 */
void App_OV5640_Thread_Start(void)
{
    BaseType_t xReturn = pdPASS;
    s_camera_running = 1U;
      xReturn = xTaskCreate((TaskFunction_t)Camera_Thread,
                        (char *)"Camera_Thread",
                        (uint16_t)512,
                        (void *)NULL,
                        (uint8_t)5,
                        (TaskHandle_t *)NULL);
  if (xReturn != pdPASS)
  {
    s_camera_running = 0U;
    return;
  }
}

/**
 * @brief  初始化 OV5640，检查芯片并切换一次摄像头 LED
 */
void app_ov5640_init(void)
{
    uint8_t ret;

    LOG_INFO("CM7","OV5640 init start");

    ret = ov5640_init();
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7","OV5640 init FAILED (err=%d)", ret);
        return;
    }

    ov5640_led_on();
    ov5640_led_off();
    
    LOG_INFO("CM7","OV5640 init OK, chip ID verified");
}

/**
 * @brief  配置 OV5640 输出 800x480 RGB565 色条并采集到 LTDC 帧缓冲
 */
void app_ov5640_capture_colorbar(void)
{
    uint8_t ret = 0;

    LOG_INFO("CM7","OV5640 capture colorbar start");
    /* 1. 设置输出格式为 RGB565 */
    ret = ov5640_set_output_format(OV5640_OUTPUT_FORMAT_RGB565);
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7","OV5640 set output format failed (err=%d)", ret);
        return;
    }
    /* 2. 设置输出尺寸为 800x480 */
    ret = ov5640_set_output_size(800, 480);
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7","OV5640 set output size failed (err=%d)", ret);
        return;
    }
    /* 3. 设置测试模式为 颜色条测试 */
    ret = ov5640_set_test_pattern(OV5640_TEST_PATTERN_COLOR_BAR);
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7","OV5640 set test pattern failed (err=%d)", ret);
        return;
    }
    /* 4. 开始捕获 */
    ret = ov5640_get_frame(LTDC_FRAME_BUF_ADDR,         // 0xD0000000
                         OV5640_GET_TYPE_DTS_16B_INC, // RGB565 = 2字节/像素
                         NULL);                      // 帧数据传输前无需要完成的工作
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7","OV5640 get frame failed (err=%d)", ret);
        return;
    }
    LOG_INFO("CM7","Colorbar frame captured to 0x%08X", LTDC_FRAME_BUF_ADDR);
}

/**
 * @brief  采集色条和真实画面并打印像素信息，随后持续采集真实画面
 * @return 配置或采集失败时返回 OV5640 错误码；正常运行时持续采集，不返回
 */
uint8_t app_ov5640_run(void)
{
    uint8_t ret;

    ret = ov5640_set_output_format(OV5640_OUTPUT_FORMAT_RGB565);
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7", "ov5640_set_output_format failed (err=%d)", ret);
        return ret;
    }
    ret = ov5640_set_output_size(800, 480);
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7", "ov5640_set_output_size failed (err=%d)", ret);
        return ret;
    }
    ret = ov5640_set_light_mode(OV5640_LIGHT_MODE_ADVANCED_AWB);
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7", "ov5640_set_light_mode failed (err=%d)", ret);
        return ret;
    }
    ret = ov5640_set_color_saturation(OV5640_COLOR_SATURATION_3);
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7", "ov5640_set_color_saturation failed (err=%d)", ret);
        return ret;
    }
    ret = ov5640_set_brightness(OV5640_BRIGHTNESS_4);
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7", "ov5640_set_brightness failed (err=%d)", ret);
        return ret;
    }
    ret = ov5640_set_contrast(OV5640_CONTRAST_4);
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7", "ov5640_set_contrast failed (err=%d)", ret);
        return ret;
    }
    ret = ov5640_set_hue(OV5640_HUE_6);
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7", "ov5640_set_hue failed (err=%d)", ret);
        return ret;
    }
    ret = ov5640_set_exposure_level(OV5640_EXPOSURE_LEVEL_5);
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7", "ov5640_set_exposure_level failed (err=%d)", ret);
        return ret;
    }
    ret = ov5640_set_sharpness_level(OV5640_SHARPNESS_5);
    if (ret != OV5640_EOK)
    {
        LOG_ERROR("CM7", "ov5640_set_sharpness_level failed (err=%d)", ret);
        return ret;
    }

    /* 三帧 SDRAM：显示帧、采集帧、待显示帧。DMA 持续接收，LTDC 在消隐期换帧。 */
    /* 避开从 0xD0110000 开始的 JPEG 工作缓冲，采集帧放在独立区域。 */
    LOG_INFO("CM7", "CAMERA_STREAM_START");
    while (s_pause_requested != 0U)
    {
        s_paused = 1U;
        osDelay(1U);
    }
    s_paused = 0U;
    uint8_t streaming = 0U;
    uint32_t retries = 0U;
    for (;;)
    {
        if (s_pause_requested != 0U)
        {
            ov5640_dcmi_stream_stop();
            streaming = 0U;
            if (((DCMI->CR & DCMI_CR_CAPTURE) != 0U) ||
                ((((DMA_Stream_TypeDef *)hdcmi.DMA_Handle->Instance)->CR & DMA_SxCR_EN) != 0U))
            {
                osDelay(10U);
                continue;
            }
            while ((DMA2D->CR & DMA2D_CR_START) != 0U)
            {
                osDelay(1U);
            }
            s_paused = 1U;
            while (s_pause_requested != 0U)
            {
                osDelay(1U);
            }
            s_paused = 0U;
        }
        if (!streaming)
        {
            /* 暂停请求可能在上次等待期间到达，不在 OTA 暂停中重启 DMA。 */
            if (s_pause_requested) continue;
            ret = ov5640_dcmi_stream_start(LTDC_FRAME_BUF_ADDR, 0xD0300000U, 800U * 480U * 2U);
            if (ret != OV5640_DCMI_EOK)
            {
                ov5640_dcmi_stream_log_error();
                ov5640_dcmi_stream_stop();
                LOG_ERROR("CM7", "CAMERA_RESTART_FAIL=%u retry=%lu", ret, (unsigned long)++retries);
                osDelay(250U);
                continue;
            }
            streaming = 1U;
            if (retries) LOG_INFO("CM7", "CAMERA_RESTART_STARTED retry=%lu", (unsigned long)retries);
        }
        ret = ov5640_dcmi_stream_poll();
        if (ret != OV5640_DCMI_EOK)
        {
            ov5640_dcmi_stream_log_error();
            LOG_ERROR("CM7", "CAMERA_STREAM_FAIL=%u DCMI=0x%lX DMA=0x%lX",
                      ret, (unsigned long)hdcmi.ErrorCode,
                      (unsigned long)hdcmi.DMA_Handle->ErrorCode);
            ov5640_dcmi_stream_stop();
            streaming = 0U;
            retries++;
            LOG_WARN("CM7", "CAMERA_RECOVER retry=%lu", (unsigned long)retries);
            osDelay(250U);
            continue;
        }
        osDelay(1);
    }
}
