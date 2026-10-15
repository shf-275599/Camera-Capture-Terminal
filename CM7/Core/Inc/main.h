/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define OV5640_SCL_Pin GPIO_PIN_6
#define OV5640_SCL_GPIO_Port GPIOB
#define OV5640_SDA_Pin GPIO_PIN_7
#define OV5640_SDA_GPIO_Port GPIOB
#define KEY0_Pin GPIO_PIN_8
#define KEY0_GPIO_Port GPIOA
#define OV5640_RST_Pin GPIO_PIN_2
#define OV5640_RST_GPIO_Port GPIOH
#define OV5640_PWDN_Pin GPIO_PIN_3
#define OV5640_PWDN_GPIO_Port GPIOH
#define LED0_GREEN_Pin GPIO_PIN_3
#define LED0_GREEN_GPIO_Port GPIOA
#define LTDC_BL_Pin GPIO_PIN_0
#define LTDC_BL_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */
#define KEY0_Pin GPIO_PIN_8
#define KEY0_GPIO_Port GPIOA

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
