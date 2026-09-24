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
#include "dma.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* Include 头文件 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "boot_slot.h"
#include "boot_config.h"
#include "boot_jump.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* Private TypeDefinition 自定义类型 */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* Private Macro 带参数的宏函数 */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* Private Variables 私有变量 */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* Private Function Prototypes 私有函数原型 */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
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
  /* USER CODE END Boot_Mode_Sequence_0 */

  /* USER CODE BEGIN Boot_Mode_Sequence_1 */
  /* USER CODE END Boot_Mode_Sequence_1 */
  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();
  /* USER CODE BEGIN Boot_Mode_Sequence_2 */
  /* USER CODE END Boot_Mode_Sequence_2 */

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  printf("\n");
  printf("\033[1;36m"); // 加粗；蓝色
  printf(" ____   ___   ___  _____ _      ___    _    ____  _____ ____          \n");
  printf("| __ ) / _ \\ / _ \\|_   _| |    / _ \\  / \\  |  _ \\| ____|  _ \\   \n");
  printf("|  _ \\| | | | | | | | | | |   | | | |/ _ \\ | | | |  _| | |_) |      \n");
  printf("| |_) | |_| | |_| | | | | |___| |_| / ___ \\| |_| | |___|  _ <        \n");
  printf("|____/ \\___/ \\___/  |_| |_____|\\___/_/   \\_\\____/|_____|_| \\_\\ \n");
  printf("\n");
  const uint32_t cm7_reserve_size =
      CM7_APP_SLOT_B_BASE - (CM7_APP_SLOT_A_BASE + CM7_APP_SLOT_A_SIZE);
  const uint32_t cm4_reserve_size =
      CM4_APP_SLOT_B_BASE - (CM4_APP_SLOT_A_BASE + CM4_APP_SLOT_A_SIZE);

  printf("============= flash partition table ==============\r\n");
  printf("---------- CM7 (Bank1, 1MB) ----------\r\n");
  printf("|     name     |   offset   |    size    | value |\r\n");
  printf("--------------------------------------------------\r\n");
  printf("| bootloader   | 0x%08lx | 0x%08x | %dKB |\r\n", (unsigned long)CM7_BOOT_FLASH_BASE, CM7_BOOT_FLASH_SIZE, CM7_BOOT_FLASH_SIZE / 1024U);
  printf("| slot A       | 0x%08lx | 0x%08x | %dKB |\r\n", (unsigned long)CM7_APP_SLOT_A_BASE, CM7_APP_SLOT_A_SIZE, CM7_APP_SLOT_A_SIZE / 1024U);
  printf("| reserve(S4)  | 0x%08lx | 0x%08x | %dKB |\r\n", (unsigned long)(CM7_APP_SLOT_A_BASE + CM7_APP_SLOT_A_SIZE), (unsigned int)cm7_reserve_size, (int)(cm7_reserve_size / 1024U));
  printf("| slot B       | 0x%08lx | 0x%08x | %dKB |\r\n", (unsigned long)CM7_APP_SLOT_B_BASE, CM7_APP_SLOT_B_SIZE, CM7_APP_SLOT_B_SIZE / 1024U);
  printf("---------- CM4 (Bank2, 1MB) ----------\r\n");
  printf("|     name     |   offset   |    size    | value |\r\n");
  printf("--------------------------------------------------\r\n");
  printf("| stub         | 0x%08lx | 0x%08x | %dKB |\r\n", (unsigned long)CM4_BOOT_STUB_BASE, CM4_BOOT_STUB_SIZE, CM4_BOOT_STUB_SIZE / 1024U);
  printf("| slot A       | 0x%08lx | 0x%08x | %dKB |\r\n", (unsigned long)CM4_APP_SLOT_A_BASE, CM4_APP_SLOT_A_SIZE, CM4_APP_SLOT_A_SIZE / 1024U);
  printf("| reserve(S4)  | 0x%08lx | 0x%08x | %dKB |\r\n", (unsigned long)(CM4_APP_SLOT_A_BASE + CM4_APP_SLOT_A_SIZE), (unsigned int)cm4_reserve_size, (int)(cm4_reserve_size / 1024U));
  printf("| slot B       | 0x%08lx | 0x%08x | %dKB |\r\n", (unsigned long)CM4_APP_SLOT_B_BASE, CM4_APP_SLOT_B_SIZE, CM4_APP_SLOT_B_SIZE / 1024U);
  printf("==================================================\r\n");
  printf("\n");

  // if (Boot_IsAppValid() != true)
  // {
  //   printf("application1 is invalid\r\n");
  //   return 0;
  // }
  // printf("application1 is valid\r\n");
  // Boot_JumpToAPP();

  /* USER CODE END 2 */

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

  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
  {
  }

  /** Initializes the RCC Oscillators according to the specified parameters
   * in the RCC_OscInitTypeDef structure.
   */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
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
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
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
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
