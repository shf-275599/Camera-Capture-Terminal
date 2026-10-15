/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * File Name          : freertos.c
 * Description        : Code for freertos applications
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "FreeRTOS.h"
#include "cmsis_os2.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "main.h"
#include "openamp.h"
#include "jpeg_test_data.h"
#include "app_sw_jpeg.h"
#include "app_hw_jpeg.h"
#include "jpeg_utils.h"
#include "app_ipc_cm7.h"
#include "app_ov5640.h"
#include "app_http_server.h"
#include "mylog.h"
#include "usart.h"
#include "iwdg.h"
#include "boot_eeprom.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
static StaticTask_t s_key0_task;
static StackType_t s_key0_stack[1024];

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
    .name = "defaultTask",
    .stack_size = 128 * 4,
    .priority = (osPriority_t)osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void OpenAMP_Task(void *argument);
static void Key0_AB_Test_Thread(void *argument);
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

extern void MX_LWIP_Init(void);
void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
 * @brief  FreeRTOS initialization
 * @param  None
 * @retval None
 */
void MX_FREERTOS_Init(void)
{
  /* USER CODE BEGIN Init */
  // jpeg_sw_decode(jpeg_test_data, sizeof(jpeg_test_data), 0, 0);
  // JPEG_InitColorTables();
  // if (jpeg_hw_decode(jpeg_test_data, sizeof(jpeg_test_data), 0U, 0U) != 0)
  // {
  //     printf("CM7: Hardware JPEG decode failed\n");
  // }
  LOG_INFO("CM7", "FreeRTOS init OK");
  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  USART_Rx_Thread_Start();
  App_IPC_Thread_Start();
  App_OV5640_Thread_Start();
  if (xTaskCreateStatic(Key0_AB_Test_Thread, "Key0AB", 1024, NULL, 1,
                        s_key0_stack, &s_key0_task) == NULL)
  {
    LOG_ERROR("AB", "KEY0 task creation FAIL");
  }
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */
}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
 * @brief  Function implementing the defaultTask thread.
 * @param  argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* init code for LWIP */
  MX_LWIP_Init();
  /* USER CODE BEGIN StartDefaultTask */
  HAL_IWDG_Refresh(&hiwdg1);
  App_HTTP_Server_Thread_Start();
  /* Infinite loop */
  for (;;)
  {
    HAL_IWDG_Refresh(&hiwdg1); // 周期喂狗，调度器正常运行即视为健康
    HAL_GPIO_TogglePin(LED0_GREEN_GPIO_Port, LED0_GREEN_Pin);
    osDelay(1000);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/**
 * @brief  KEY0 切换测试线程
 * @note 该线程用于测试 KEY0 切换功能，当 KEY0 被按下时，切换当前运行的槽位（A 或 B）。
 */
static void Key0_AB_Test_Thread(void *argument)
{
  static boot_eeprom_meta_t meta;
  uint8_t armed = 0U;
  uint8_t stable = 0U;
  (void)argument;

  LOG_INFO("CM7-Slot-A/B", "KEY0 ready: PA8 high=pressed, running=%u base=0x%08lX",
           (unsigned int)boot_get_running_slot(),
           (unsigned long)boot_slot_base_get(boot_get_running_slot()));
  for (;;)
  {
    if (HAL_GPIO_ReadPin(KEY0_GPIO_Port, KEY0_Pin) == GPIO_PIN_RESET)
    {
      if (stable < 3U)
      {
        stable++;
      }
      if (stable == 3U)
      {
        armed = 1U;
      }
    }
    else
    {
      stable = 0U;
      if (armed != 0U)
      {
        osDelay(40U);
        if (HAL_GPIO_ReadPin(KEY0_GPIO_Port, KEY0_Pin) == GPIO_PIN_SET)
        {
          uint8_t running = boot_get_running_slot();
          uint8_t target = (running == CM7_APP_SLOT_A_INDEX) ? CM7_APP_SLOT_B_INDEX : CM7_APP_SLOT_A_INDEX;
          uint32_t size = CM7_APP_SLOT_A_SIZE;
          uint32_t crc = 0xFFFFFFFFU;
          const uint8_t *image = (const uint8_t *)boot_slot_base_get(target);
          armed = 0U;
          if ((boot_slot_index_is_valid(running) == 0U) ||
              (boot_eeprom_load(&meta) != 0) ||
              ((meta.flags & BOOT_EE_FLAG_PENDING) != 0U) ||
              ((meta.cm4_flags & BOOT_EE_FLAG_CM4_PENDING) != 0U) ||
              (boot_slot_is_valid(target) == 0))
          {
            LOG_ERROR("CM7-Slot-A/B", "switch rejected: check EEPROM, pending and target vector");
            continue;
          }
          /* ST-Link 重刷镜像后旧 CRC 可能失效，按启动选择结果同步测试基线。 */
          if (meta.verified_slot != running)
          {
            if (boot_eeprom_select_boot_slot(&meta) != running)
            {
              LOG_ERROR("CM7-Slot-A/B", "baseline running slot mismatch");
              continue;
            }
            meta.verified_slot = running;
            if (boot_eeprom_save(&meta) != 0)
            {
              LOG_ERROR("CM7-Slot-A/B", "baseline sync FAIL");
              continue;
            }
            LOG_INFO("CM7-Slot-A/B", "baseline synced: verified=%u", (unsigned int)running);
          }
          for (uint32_t i = 0U; i < size; i++)
          {
            crc ^= image[i];
            for (uint32_t bit = 0U; bit < 8U; bit++)
            {
              crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
            }
            if ((i & 4095U) == 0U)
            {
              HAL_IWDG_Refresh(&hiwdg1);
              osDelay(1U);
            }
          }
          crc = ~crc;
          LOG_INFO("CM7-Slot-A/B", "switch %u -> %u base=0x%08lX full_slot_size=%lu crc=0x%08lX",
                   (unsigned int)running, (unsigned int)target,
                   (unsigned long)boot_slot_base_get(target), (unsigned long)size, (unsigned long)crc);
          if (boot_eeprom_set_pending(&meta, target, size, crc, meta.app_version[target]) != 0)
          {
            LOG_ERROR("CM7-Slot-A/B", "pending save FAIL");
            continue;
          }
          LOG_INFO("CM7-Slot-A/B", "pending saved: slot=%u trial=%u seq=%lu; reboot",
                   (unsigned int)meta.pending_slot, (unsigned int)meta.boot_trial, (unsigned long)meta.seq);
          osDelay(100U);
          HAL_NVIC_SystemReset();
        }
      }
    }
    osDelay(20U);
  }
}

/* USER CODE END Application */
