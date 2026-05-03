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
#include "spi.h"
#include "tim.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "tim.h"
#include "led_matrix.h"
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
/* USER CODE BEGIN PV */
uint8_t currentFrame = 0;  // 当前显示的帧索引 (0-119)
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
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

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_TIM3_Init();
  MX_TIM2_Init();
  MX_TIM8_Init();
  MX_SPI1_Init();

  /* USER CODE BEGIN 2 */
  uint16_t g_led_frame[20]={0x0000,0xFFFF,0x0000,0xFFFF,0x0000,0xFFFF,0x0000,0xFFFF,0x0000,0xFFFF,0x0000,0xFFFF,0x0000,0xFFFF,0x0000,0xFFFF,0x0000,0xFFFF,0x0000};

  // 配置DMA回调函数（可选，但建议）
  hdma_tim3_up.XferErrorCallback = Error_Handler;
  hdma_tim8_up.XferErrorCallback = Error_Handler;
  
  // 启动定时器（必须）
  HAL_TIM_Base_Start(&htim3);
  HAL_TIM_Base_Start(&htim8);
  
  // 使能定时器DMA请求（必须）
  __HAL_TIM_ENABLE_DMA(&htim3, TIM_DMA_UPDATE);
  __HAL_TIM_ENABLE_DMA(&htim8, TIM_DMA_UPDATE);
  
  // 初始化LED缓冲区
  ledBufferInit();
  
  // 定义颜色数组（8种颜色）
  uint32_t colors[8] = {
    0xFF0000,  // 红色
    0x00FF00,  // 绿色
    0x0000FF,  // 蓝色
    0xFFFF00,  // 黄色
    0xFF00FF,  // 紫色
    0x00FFFF,  // 青色
    0xFF8800,  // 橙色
    0xFF88FF   // 粉色
  };
  
  // 初始化120个柱面的流水灯效果
  // 顺序：displayMem[0].A -> displayMem[0].B -> displayMem[1].A -> displayMem[1].B...
  for (int frameIdx = 0; frameIdx < CYLINDER_NUM; frameIdx++) {
    // 计算当前frame对应的displayMem索引和buffer类型(A/B)
    int memIdx = frameIdx / 2;
    uint8_t isBufferA = (frameIdx % 2 == 0);
    
    // 计算当前frame对应的LED位置(0-15)和颜色索引
    int ledPos = frameIdx % ONE_BUS_LED_NUM;
    int colorIdx = frameIdx / ONE_BUS_LED_NUM;
    
    // 获取当前颜色
    uint32_t color = colors[colorIdx % 8];
    
    // 清除当前buffer
    if (isBufferA) {
      ledBufferClear(displayMem[memIdx].ledBufferA);
      // 在当前LED位置设置颜色
      for (int io = 0; io < 16; io++) {
        ledSetColorOne(displayMem[memIdx].ledBufferA, ledPos, io, color);
      }
    } else {
      ledBufferClear(displayMem[memIdx].ledBufferB);
      // 在当前LED位置设置颜色
      for (int io = 0; io < 16; io++) {
        ledSetColorOne(displayMem[memIdx].ledBufferB, ledPos, io, color);
      }
    }
  }
  //ledBufferClear(displayMem[0].ledBufferA); // 初始状态全灭
  //ledSetColorOne(displayMem[0].ledBufferA, 1, 11, 0xFF0000); // 第一个LED红色
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    // 计算当前frame对应的displayMem索引和buffer类型(A/B)
    int memIdx = currentFrame / 2;
    uint8_t isBufferA = (currentFrame % 2 == 0);
    
    // 启动DMA传输显示当前帧
    if (isBufferA) {
      HAL_DMA_Start_IT(&hdma_tim3_up, (uint32_t)displayMem[memIdx].ledBufferA, (uint32_t)&GPIOD->ODR, ONE_BUS_LED_NUM*24*4);
      //HAL_DMA_Start_IT(&hdma_tim8_up, (uint32_t)displayMem[memIdx].ledBufferB, (uint32_t)&GPIOE->ODR, ONE_BUS_LED_NUM*24*4);
    } else {
      HAL_DMA_Start_IT(&hdma_tim3_up, (uint32_t)displayMem[memIdx].ledBufferB, (uint32_t)&GPIOE->ODR, ONE_BUS_LED_NUM*24*4);
      //HAL_DMA_Start_IT(&hdma_tim8_up, (uint32_t)displayMem[memIdx].ledBufferA, (uint32_t)&GPIOE->ODR, ONE_BUS_LED_NUM*24*4);
    }
    //HAL_DMA_Start_IT(&hdma_tim3_up, (uint32_t)displayMem[0].ledBufferA, (uint32_t)&GPIOE->ODR, ONE_BUS_LED_NUM*24*4);
    // 延迟一段时间
    HAL_Delay(50);
    
    // 更新当前帧索引（循环120个柱面）
    currentFrame = (currentFrame + 1) % CYLINDER_NUM;
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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 60;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 5;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
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

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM2)
  {

  }
}

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  HAL_MPU_Disable();

  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */