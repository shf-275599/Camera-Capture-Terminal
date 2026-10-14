/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
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
#include "main.h"
#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "dcmi.h"
#include "dma.h"
#include "dma2d.h"
#include "i2c.h"
#include "iwdg.h"
#include "jpeg.h"
#include "ltdc.h"
#include "lwip.h"
#include "mdma.h"
#include "openamp.h"
#include "usart.h"
#include "gpio.h"
#include "fmc.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* Include 头文件 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "mpu.h"
#include "bsp_sys.h"
#include "sdram.h"
#include "lcd.h"
#include "app_ipc_cm7.h"
#include "app_ov5640.h"
#include "boot_slot.h"
#include "cm4_boot_handoff.h"
#include "mylog.h"
#include "bsp_eeprom.h"
#include "boot_eeprom.h"
#include "eth.h"
#include "eth_phy.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* Private TypeDefinition 自定义类型 */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* Private Define 自定义宏常量 */

/* DUAL_CORE_BOOT_SYNC_SEQUENCE: Define for dual core boot synchronization    */
/*                             demonstration code based on hardware semaphore */
/* This define is present in both CM7/CM4 projects                            */
/* To comment when developping/debugging on a single core                     */
#define DUAL_CORE_BOOT_SYNC_SEQUENCE

#if defined(DUAL_CORE_BOOT_SYNC_SEQUENCE)
#ifndef HSEM_ID_0
#define HSEM_ID_0 (0U) /* HW semaphore 0*/
#endif
#endif /* DUAL_CORE_BOOT_SYNC_SEQUENCE */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* Private Macro 带参数的宏函数 */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* Private Variables 私有变量 */
/* Boot */
static boot_eeprom_meta_t ee;          // 保存 EEPROM 中的 active、pending 等启动记录
static volatile uint8_t running;       // 当前运行的槽槽号
static volatile uint8_t cm4_slot;      // CM4 启动的槽槽号
static volatile uint32_t cm4_base;     // CM4 启动的基地址
static volatile int ee_ok;             // EEPROM 是否成功读取
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */
/* Private Function Prototypes 私有函数原型 */
static void ipc_share_region_clear(void)
{
  memset((void *)0x38000000, 0, 0x00010000); // 启动前清空 64KB SRAM4；后续会在这里写 CM4 handoff
}
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */
/* USER CODE BEGIN Boot_Mode_Sequence_0 */
#if defined(DUAL_CORE_BOOT_SYNC_SEQUENCE)
  int32_t timeout;
#endif /* DUAL_CORE_BOOT_SYNC_SEQUENCE */
/* USER CODE END Boot_Mode_Sequence_0 */

/* USER CODE BEGIN Boot_Mode_Sequence_1 */
#if defined(DUAL_CORE_BOOT_SYNC_SEQUENCE)
  /* Wait until CPU2 boots and enters in stop mode or timeout*/
  timeout = 0xFFFF;
  while ((__HAL_RCC_GET_FLAG(RCC_FLAG_D2CKRDY) != RESET) && (timeout-- > 0))
    ;
  if (timeout < 0)
  {
    // Error_Handler();
  }
#endif /* DUAL_CORE_BOOT_SYNC_SEQUENCE */
/* USER CODE END Boot_Mode_Sequence_1 */
  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  __enable_irq();
  memory_protection_unit_init(); // 初始化内存保护单元
  scb_cache_init();              // 初始化缓存
  ipc_share_region_clear();      // 先清共享内存，再写入 CM4 启动地址
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();
/* USER CODE BEGIN Boot_Mode_Sequence_2 */
#if defined(DUAL_CORE_BOOT_SYNC_SEQUENCE)
  /* When system initialization is finished, Cortex-M7 will release Cortex-M4 by means of
  HSEM notification */
  /*HW semaphore Clock enable*/
  __HAL_RCC_HSEM_CLK_ENABLE();
  /*Take HSEM */
  HAL_HSEM_FastTake(HSEM_ID_0);
  /*Release HSEM in order to notify the CPU2(CM4)*/
  HAL_HSEM_Release(HSEM_ID_0, 0);
#endif /* DUAL_CORE_BOOT_SYNC_SEQUENCE */
/* USER CODE END Boot_Mode_Sequence_2 */

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_MDMA_Init();
  MX_DMA_Init();
  MX_FMC_Init();
  MX_LTDC_Init();
  MX_JPEG_Init();
  MX_DMA2D_Init();
  MX_DCMI_Init();
  MX_USART1_UART_Init();
  MX_I2C4_Init();
  MX_IWDG1_Init();
  /* USER CODE BEGIN 2 */
  {
    printf("\033[1;36m"); // 加粗；青色
    printf("    _     ____   ____   _      ___   ____     _     _____  ___   ___   _   _ \n");
    printf("   / \\   |  _ \\ |  _ \\ | |    |_ _| / ___|   / \\   |_   _||_ _| / _ \\ | \\ | |\n");
    printf("  / _ \\  | |_) || |_) || |     | | | |      / _ \\    | |   | | | | | ||  \\| |\n");
    printf(" / ___ \\ |  __/ |  __/ | |___  | | | |___  / ___ \\   | |   | | | |_| || |\\  |\n");
    printf("/_/   \\_\\|_|    |_|    |_____||___| \\____|/_/   \\_\\  |_|  |___| \\___/ |_| \\_|\n");
    printf("\n");

    /* 从 EEPROM 读取启动记录，并通知 CM4 Stub 启动哪个应用槽 */
    running = boot_get_running_slot(); // 根据当前运行地址判断 CM7 位于 A 槽还是 B 槽
    cm4_slot = CM4_APP_SLOT_A_INDEX;   // EEPROM 不可用时，CM4 默认从 A 槽启动
    cm4_base = CM4_APP_SLOT_A_BASE;    // CM4 默认应用的 Flash 起始地址
    ee_ok = 0;                         // 记录本次是否成功初始化 EEPROM

    if (bsp_eeprom_init() == 0) // 初始化板载 EEPROM，成功时返回 0
    {
      int ee_init_ret = boot_eeprom_init(); // 准备 Boot 元数据的 EEPROM 访问
      if (ee_init_ret != 0)
      {
        LOG_ERROR("APP", "EEPROM init fail, CM4 default Slot A"); // 初始化失败时沿用前面设置的 CM4 A 槽默认值
      }
      int ee_load_ret = boot_eeprom_load(&ee); // 从双备份读取启动记录；无有效副本时使用默认值
      if (ee_load_ret != 0)
      {
        LOG_WARN("APP", "EEPROM meta empty/invalid, using defaults (ret=%d)", ee_load_ret);
      }
      ee_ok = 1; // EEPROM 可访问，后续按启动记录选择 CM4 槽

      /* 打印当前 CM7 槽和 EEPROM 记录，便于核对启动状态 */
      LOG_INFO("APP", "CM7 running slot=%u base=0x%08lX | EE active=%u pending=%u flags=0x%02X, init ret=%d, load ret=%d",
               (unsigned int)running,
               (unsigned long)boot_slot_base_get(running),
               (unsigned int)ee.active_slot,
               (unsigned int)ee.pending_slot,
               (unsigned int)ee.flags,
               (unsigned int)ee_init_ret,
               (unsigned int)ee_load_ret);
      /* 打印 CM4 的启动槽和两个应用版本 */
      LOG_INFO("APP", "CM4 EE active=%u pending=%u flags=0x%02X verA=%lu verB=%lu",
               (unsigned int)ee.cm4_active_slot,
               (unsigned int)ee.cm4_pending_slot,
               (unsigned int)ee.cm4_flags,
               (unsigned long)ee.cm4_app_version[CM4_APP_SLOT_A_INDEX - CM4_APP_SLOT_A_INDEX],
               (unsigned long)ee.cm4_app_version[CM4_APP_SLOT_B_INDEX - CM4_APP_SLOT_A_INDEX]);

      cm4_slot = boot_eeprom_prepare_cm4_boot(&ee); // 按 pending、active、兜底顺序选择 CM4 槽，并处理试启动次数
      cm4_base = boot_slot_base_get(cm4_slot);      // 将 CM4 槽号换算成 Flash 起始地址

      // (void)bsp_eeprom_selftest(); // 测试 EEPROM 用户区读写，不参与本次槽位选择
    }
    else
    {
      LOG_ERROR("APP", "CM7 EEPROM init fail, CM4 default Slot A"); // 初始化失败时沿用前面设置的 CM4 A 槽默认值
    }

    cm4_boot_handoff_write(cm4_base); // 将 CM4 应用地址写入 SRAM4，供固定 Stub 读取
    LOG_INFO("APP", "CM4 handoff slot=%u base=0x%08lX ee=%d",
             (unsigned int)cm4_slot,
             (unsigned long)cm4_base,
             ee_ok);
    /* 使能 CM4 后，Bank2 固定 Stub 读取 handoff 并跳转到所选应用槽 */
    HAL_RCCEx_EnableBootCore(RCC_BOOT_C2);
    LOG_INFO("APP", "CM4 boot core enabled");
  }
  HAL_IWDG_Refresh(&hiwdg1); // 喂狗，防初始化链累计超过 IWDG 超时
  MX_ETH_Init();
  {
    LOG_INFO("CM7", "ETH I/O init OK");
    int32_t phy_status = eth_phy_init();
    if (phy_status != LAN8720_STATUS_OK)
    {
      eth_phy_print_diag();
      LOG_ERROR("CM7", "PHY init failed: %s", eth_phy_link_state_str(phy_status));
    }
    else
    {
      eth_phy_print_diag();
      LOG_INFO("CM7", "PHY init OK, link: %s", eth_phy_link_state_str(eth_phy_get_link_state()));
    }
  }
  app_ipc_cm7_init(); // 初始化 IPC 模块
  sdram_init();       // 初始化 SDRAM
  lcd_init();         // 初始化 LCD
  app_ov5640_init();  // 初始化 OV5640 摄像头
  LOG_INFO("CM7", "main() user init OK");

  /* OTA 试启动确认：时钟/以太网/IPC/SDRAM/LCD/摄像头全部初始化成功后才提交 */
  if (((ee.flags & BOOT_EE_FLAG_PENDING) != 0U) && (ee.pending_slot == running))
  {
    boot_eeprom_commit_pending(&ee); // 更新 active 并清除 pending，写回 EEPROM
    LOG_INFO("APP", "CM7 OTA pending committed -> active=%u",
             (unsigned int)ee.active_slot);
  }
  HAL_IWDG_Refresh(&hiwdg1); // EEPROM 写入后再喂一次
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();  /* Call init function for freertos objects (in cmsis_os2.c) */
  MX_FREERTOS_Init();

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM2 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM2)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
