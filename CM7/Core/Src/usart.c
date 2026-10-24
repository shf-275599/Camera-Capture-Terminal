/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file    usart.c
 * @brief   This file provides code for the configuration
 *          of the USART instances.
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
#include "usart.h"

/* USER CODE BEGIN 0 */
#include <stdio.h>
#include "freertos.h"
#include "task.h"
#include "cmsis_os2.h"
#include "boot_slot.h"
#include "cm4_ota_hold.h"
/****************************************************************************/
// 重定向printf到串口
#ifdef __GNUC__
// GCC编译器
int _write(int file, char *ptr, int len)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)ptr, len, HAL_MAX_DELAY);
  return len;
}
#else
// Keil MDK编译器
int fputc(int ch, FILE *f)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
  return ch;
}
#endif
/****************************************************************************/

static uint8_t Rx_Data = 0;
static volatile uint32_t rx_event_count = 0;

/**
 * @brief  串口接收完成回调函数
 * @param  huart 串口句柄
 * @retval 无
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    if (rx_event_count != UINT32_MAX)
    {
      rx_event_count++;
    }
    HAL_UART_Receive_IT(&huart1, &Rx_Data, 1);
  }
}

/**
 * @brief  串口接收完成回调函数
 * @param  huart 串口句柄
 * @retval 无
 * */
static uint32_t usart_take_rx_events(void)
{
  uint32_t count;
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  count = rx_event_count;
  rx_event_count = 0;
  __set_PRIMASK(primask);
  return count;
}

/**
 * @brief  串口接收线程
 * @param  pvParameters 无
 * @retval 无
 * */
static void USART_Rx_Thread(void *pvParameters)
{
  for (;;)
  {
    uint32_t rx_events = usart_take_rx_events();
    while (rx_events-- != 0U)
    {
      uint8_t cm7_slot = boot_get_running_slot();
      uint8_t cm4_slot = cm4_ota_running_slot();
      char message[64];
      int length = snprintf(message, sizeof(message), "CM7 running_slot: %s, CM4 running_slot: %s\r\n",
                            (cm7_slot == CM7_APP_SLOT_A_INDEX) ? "A" :
                            (cm7_slot == CM7_APP_SLOT_B_INDEX) ? "B" : "unknown",
                            (cm4_slot == CM4_APP_SLOT_A_INDEX) ? "A" :
                            (cm4_slot == CM4_APP_SLOT_B_INDEX) ? "B" : "unknown");
      HAL_UART_Transmit(&huart1, (uint8_t *)message, (uint16_t)length, 1000);
    }
    osDelay(1);
  }
}

/**
 * @brief  串口接收线程启动
 * @param  None
 * @retval None
 * */
void USART_Rx_Thread_Start(void)
{
  BaseType_t xReturn = pdPASS;
  xReturn = xTaskCreate((TaskFunction_t)USART_Rx_Thread,
                        (char *)"USART_Rx_Thread",
                        (uint16_t)128,
                        (void *)NULL,
                        (uint8_t)5,
                        (TaskHandle_t *)NULL);
  if (xReturn != pdPASS)
  {
    return;
  }
}
/* USER CODE END 0 */

UART_HandleTypeDef huart1;

/* USART1 init function */

void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */
  HAL_UART_Receive_IT(&huart1, &Rx_Data, 1);
  /* USER CODE END USART1_Init 2 */

}

void HAL_UART_MspInit(UART_HandleTypeDef* uartHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspInit 0 */

  /* USER CODE END USART1_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USART1;
    PeriphClkInitStruct.Usart16ClockSelection = RCC_USART16CLKSOURCE_D2PCLK2;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    /* USART1 clock enable */
    __HAL_RCC_USART1_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**USART1 GPIO Configuration
    PA10     ------> USART1_RX
    PA9     ------> USART1_TX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_10|GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* USART1 interrupt Init */
    HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
  /* USER CODE BEGIN USART1_MspInit 1 */

  /* USER CODE END USART1_MspInit 1 */
  }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef* uartHandle)
{

  if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspDeInit 0 */

  /* USER CODE END USART1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_USART1_CLK_DISABLE();

    /**USART1 GPIO Configuration
    PA10     ------> USART1_RX
    PA9     ------> USART1_TX
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_10|GPIO_PIN_9);

    /* USART1 interrupt Deinit */
    HAL_NVIC_DisableIRQ(USART1_IRQn);
  /* USER CODE BEGIN USART1_MspDeInit 1 */

  /* USER CODE END USART1_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

