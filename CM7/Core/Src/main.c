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
#include "i2c.h"
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
#include "mylog.h"
#include "bsp_eeprom.h"
#include "boot_eeprom.h"
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
static boot_eeprom_meta_t ee;          // EEPROM 中的 active、pending、试启动次数等记录
static uint8_t boot_slot;              // 本次选中的 CM7 槽号
static uint32_t app_base;              // 本次选中的 CM7 应用 Flash 起始地址
static volatile uint8_t cm7_slot_a_ok; // CM7 A 槽向量表检查结果
static volatile uint8_t cm7_slot_b_ok; // CM7 B 槽向量表检查结果
static volatile uint8_t cm4_slot_a_ok; // CM4 A 槽向量表检查结果
static volatile uint8_t cm4_slot_b_ok; // CM4 B 槽向量表检查结果
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
  int32_t timeout;
  /* USER CODE END Boot_Mode_Sequence_0 */

  /* USER CODE BEGIN Boot_Mode_Sequence_1 */
  /* 等待 CM4 进入 STOP，超时后继续启动，避免 CM4 未烧录时卡住 */
  timeout = 0xFFFF; // 限制等待次数
  while ((__HAL_RCC_GET_FLAG(RCC_FLAG_D2CKRDY) != RESET) && (timeout-- > 0))
    ; // D2 时钟仍就绪说明 CM4 尚未进入 STOP
  if (timeout < 0)
  {
    /* CM4 未进 STOP；跳转函数随后会禁止 CM4 从固定入口启动 */
  }
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
  MX_I2C4_Init();
  /* USER CODE BEGIN 2 */
  printf("\r\n");
  printf("\033[1;36m"); // 加粗；蓝色
  printf(" ____   ___   ___  _____ _      ___    _    ____  _____ ____          \n");
  printf("| __ ) / _ \\ / _ \\|_   _| |    / _ \\  / \\  |  _ \\| ____|  _ \\   \n");
  printf("|  _ \\| | | | | | | | | | |   | | | |/ _ \\ | | | |  _| | |_) |      \n");
  printf("| |_) | |_| | |_| | | | | |___| |_| / ___ \\| |_| | |___|  _ <        \n");
  printf("|____/ \\___/ \\___/  |_| |_____|\\___/_/   \\_\\____/|_____|_| \\_\\ \n");
  printf("\r\n");
  /* 两段预留区大小由相邻应用槽地址算出，仅用于打印分区表 */
  const uint32_t cm7_reserve_size =
      CM7_APP_SLOT_B_BASE - (CM7_APP_SLOT_A_BASE + CM7_APP_SLOT_A_SIZE);
  const uint32_t cm4_reserve_size =
      CM4_APP_SLOT_B_BASE - (CM4_APP_SLOT_A_BASE + CM4_APP_SLOT_A_SIZE);
  printf("BOOT Tag: %s\r\n", BOOT_FW_TAG); // 打印固件标签
  printf("\n");
  /* 打印双核 Flash 分区，便于对照实际烧录位置 */
  printf("============= flash partition table ==============\r\n");
  printf("---------------- CM7 (Bank1, 1MB) ----------------\r\n");
  printf("|     name     |   offset   |    size    | value |\r\n");
  printf("--------------------------------------------------\r\n");
  printf("| bootloader   | 0x%08lx | 0x%08x | %dKB |\r\n", (unsigned long)CM7_BOOT_FLASH_BASE, CM7_BOOT_FLASH_SIZE, CM7_BOOT_FLASH_SIZE / 1024U);
  printf("| slot A       | 0x%08lx | 0x%08x | %dKB |\r\n", (unsigned long)CM7_APP_SLOT_A_BASE, CM7_APP_SLOT_A_SIZE, CM7_APP_SLOT_A_SIZE / 1024U);
  printf("| reserve(S4)  | 0x%08lx | 0x%08x | %dKB |\r\n", (unsigned long)(CM7_APP_SLOT_A_BASE + CM7_APP_SLOT_A_SIZE), (unsigned int)cm7_reserve_size, (int)(cm7_reserve_size / 1024U));
  printf("| slot B       | 0x%08lx | 0x%08x | %dKB |\r\n", (unsigned long)CM7_APP_SLOT_B_BASE, CM7_APP_SLOT_B_SIZE, CM7_APP_SLOT_B_SIZE / 1024U);
  printf("---------------- CM4 (Bank2, 1MB) ----------------\r\n");
  printf("|     name     |   offset   |    size    | value |\r\n");
  printf("--------------------------------------------------\r\n");
  printf("| stub         | 0x%08lx | 0x%08x | %dKB |\r\n", (unsigned long)CM4_BOOT_STUB_BASE, CM4_BOOT_STUB_SIZE, CM4_BOOT_STUB_SIZE / 1024U);
  printf("| slot A       | 0x%08lx | 0x%08x | %dKB |\r\n", (unsigned long)CM4_APP_SLOT_A_BASE, CM4_APP_SLOT_A_SIZE, CM4_APP_SLOT_A_SIZE / 1024U);
  printf("| reserve(S4)  | 0x%08lx | 0x%08x | %dKB |\r\n", (unsigned long)(CM4_APP_SLOT_A_BASE + CM4_APP_SLOT_A_SIZE), (unsigned int)cm4_reserve_size, (int)(cm4_reserve_size / 1024U));
  printf("| slot B       | 0x%08lx | 0x%08x | %dKB |\r\n", (unsigned long)CM4_APP_SLOT_B_BASE, CM4_APP_SLOT_B_SIZE, CM4_APP_SLOT_B_SIZE / 1024U);
  printf("==================================================\r\n");
  printf("USART1 Baudrate: 115200, Pins: PA9/PA10\r\n");  // 打印 USART1 配置
  printf("EEPROM Peripheral: I2C4, Pins: PD12/PD13\r\n"); // 打印 EEPROM 配置
  printf("\r\n");

  LOG_INFO("BOOT","=================================================================================");
  LOG_INFO("BOOT", "Initializing EEPROM...");
  if (bsp_eeprom_init() == 0) // 初始化板载 EEPROM；成功返回 0
  {
    boot_eeprom_init();                     // 准备 Boot 元数据的 EEPROM 访问
    LOG_INFO("BOOT", "EEPROM initialized"); // 初始化成功

    LOG_INFO("BOOT", "Loading EEPROM metadata...");
   int ee_load_ret = boot_eeprom_load(&ee); // 读取 EEPROM 中有效的元数据，有效时返回 0，否则返回 1
    if (ee_load_ret == 1)                // 无有效启动记录，首次写入默认值，通常是全新片或已损坏的情况。
    {
      LOG_INFO("BOOT", "EEPROM empty, saving default metadata"); // 双备份均无效，提示写入首份默认记录

      if (boot_eeprom_save(&ee) == 0) // 将默认启动记录写入 EEPROM
      {
        LOG_INFO("BOOT", "EEPROM default metadata saved"); // 默认元数据落盘成功
      }
      else // 保存失败：EEPROM 掉线或写超时
      {
        LOG_ERROR("BOOT", "EEPROM default metadata save failed"); // 落盘失败：本次仍按内存默认值继续，下次开机重试
      }
    }
    else // 有有效启动记录，直接使用
    {
      LOG_INFO("BOOT", "EEPROM metadata loaded"); // 读取成功
    }
  }

  /* 仅检查栈指针和复位入口等向量表条件，不计算整个镜像的 CRC */
  LOG_INFO("BOOT", "Checking CM7 slots...");
  cm7_slot_a_ok = (uint8_t)boot_slot_is_valid(CM7_APP_SLOT_A_INDEX); // 检查 CM7 A 槽向量表
  cm7_slot_b_ok = (uint8_t)boot_slot_is_valid(CM7_APP_SLOT_B_INDEX); // 检查 CM7 B 槽向量表
  if (cm7_slot_a_ok)
    LOG_INFO("BOOT", "CM7 slot A: valid");
  else
    LOG_WARN("BOOT", "CM7 slot A: invalid");
  if (cm7_slot_b_ok)
    LOG_INFO("BOOT", "CM7 slot B: valid");
  else
    LOG_WARN("BOOT", "CM7 slot B: invalid");

  LOG_INFO("BOOT", "Checking CM4 slots...");
  cm4_slot_a_ok = (uint8_t)boot_slot_is_valid(CM4_APP_SLOT_A_INDEX); // 检查 CM4 A 槽向量表，仅用于打印
  cm4_slot_b_ok = (uint8_t)boot_slot_is_valid(CM4_APP_SLOT_B_INDEX); // 检查 CM4 B 槽向量表，仅用于打印
  if (cm4_slot_a_ok)
    LOG_INFO("BOOT", "CM4 slot A: valid");
  else
    LOG_WARN("BOOT", "CM4 slot A: invalid");
  if (cm4_slot_b_ok)
    LOG_INFO("BOOT", "CM4 slot B: valid");
  else
    LOG_WARN("BOOT", "CM4 slot B: invalid");

  LOG_INFO("BOOT", "CM7 EEPROM metadata: active=%u pending=%u trial=%u flags=0x%02X seq=%lu", // 打印 CM7 EEPROM 元数据
           (unsigned int)ee.active_slot,                                                      // 激活槽位索引
           (unsigned int)ee.pending_slot,                                                     // 待处理槽位索引
           (unsigned int)ee.boot_trial,                                                       // 启动试用次数
           (unsigned int)ee.flags,                                                            // 标志位
           (unsigned long)ee.seq);                                                            // 序列号

  LOG_INFO("BOOT", "CM7 EEPROM metadata version A=%lu B=%lu | slotA valid=%u slotB valid=%u", // 打印 EEPROM 应用版本
           (unsigned long)ee.app_version[CM7_APP_SLOT_A_INDEX],                               // 应用槽位 A 版本号
           (unsigned long)ee.app_version[CM7_APP_SLOT_B_INDEX],                               // 应用槽位 B 版本号
           (unsigned int)cm7_slot_a_ok,                                                       // 检查槽位 A 是否有效
           (unsigned int)cm7_slot_b_ok);                                                      // 检查槽位 B 是否有效

  LOG_INFO("BOOT", "CM4 EEPROM metadata: active=%u pending=%u trial=%u flags=0x%02X | ver A=%lu B=%lu", // 打印 CM4 EEPROM 元数据
           (unsigned int)ee.cm4_active_slot,                                                            // 激活槽位索引
           (unsigned int)ee.cm4_pending_slot,                                                           // 待处理槽位索引
           (unsigned int)ee.cm4_boot_trial,                                                             // 启动试用次数
           (unsigned int)ee.cm4_flags,                                                                  // 标志位
           (unsigned long)ee.cm4_app_version[CM4_APP_SLOT_A_INDEX - CM4_APP_SLOT_A_INDEX],              // 应用槽位 A 版本号
           (unsigned long)ee.cm4_app_version[CM4_APP_SLOT_B_INDEX - CM4_APP_SLOT_A_INDEX]);             // 应用槽位 B 版本号

  /* 按 pending、active、A、B 的顺序选 CM7 槽；试启动可能扣减次数并回退 */
  LOG_INFO("BOOT", "Preparing boot slot and checking CM7 slots...");
  boot_slot = boot_eeprom_prepare_boot(&ee); // 选择启动槽，并在需要时更新 EEPROM 试启动状态
  app_base = boot_slot_base_get(boot_slot);  // 将槽号换算为应用向量表的 Flash 地址
  if (boot_app_is_valid(app_base)) // 再次确认目标槽地址及向量表符合启动条件
  {
    uint32_t sp = *(__IO uint32_t *)app_base;        // 向量表第 1 项：应用启动时的栈顶 MSP
    uint32_t pc = *(__IO uint32_t *)(app_base + 4U); // 向量表第 2 项：应用的 Reset_Handler 地址

    LOG_INFO("BOOT", "jump slot %u @ 0x%08lX", // 打印要启动的槽号和地址
             (unsigned int)boot_slot, (unsigned long)app_base);
    LOG_INFO("BOOT", "vec SP=0x%08lX Reset=0x%08lX", // 打印应用栈顶和复位入口
             (unsigned long)sp, (unsigned long)pc);
    LOG_INFO("BOOT", "hold CM4 reset, then jump...");                // 提示跳转时将禁止 CM4 从固定入口启动
    boot_jump_to_app(app_base);                                      // 清理 Boot 中断、切换栈和向量表，跳到应用
    LOG_ERROR("BOOT", "ERROR: Jump returned (should never happen)"); // 正常跳转不返回；返回说明跳转异常
  }
  else
  {
    /* 无法启动所选 CM7 槽：打印 A/B 向量表帮助定位，随后留在 Bootloader */
    LOG_ERROR("BOOT", "no valid APP (tried slot %u @ 0x%08lX), stay in Boot.", // 报告失败槽号及地址
              (unsigned int)boot_slot, (unsigned long)app_base);
    LOG_INFO("BOOT", "  A: SP=0x%08lX Reset=0x%08lX valid=%u",              // A 槽栈顶、复位入口及检查结果
             (unsigned long)(*(__IO uint32_t *)CM7_APP_SLOT_A_BASE),        // A 槽向量表第 1 项
             (unsigned long)(*(__IO uint32_t *)(CM7_APP_SLOT_A_BASE + 4U)), // A 槽向量表第 2 项
             (unsigned int)cm7_slot_a_ok);
    LOG_INFO("BOOT", "  B: SP=0x%08lX Reset=0x%08lX valid=%u",              // B 槽栈顶、复位入口及检查结果
             (unsigned long)(*(__IO uint32_t *)CM7_APP_SLOT_B_BASE),        // B 槽向量表第 1 项
             (unsigned long)(*(__IO uint32_t *)(CM7_APP_SLOT_B_BASE + 4U)), // B 槽向量表第 2 项
             (unsigned int)cm7_slot_b_ok);
  }
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
