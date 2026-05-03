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
// 包含STM32H7系列芯片的头文件，提供寄存器定义和基本功能
#include "stm32h7xx.h"
// 包含LED矩阵驱动头文件
#include "LED_Matrix_Driver.h"
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
static uint8_t g_demo_phase = 0U;               // 演示模式相位计数器（用于动画效果）

// 外部声明的DMA句柄（在dma.c中定义）
extern DMA_HandleTypeDef hdma_tim3_up;
extern DMA_HandleTypeDef hdma_tim8_up;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
/* USER CODE BEGIN PFP */
static void LED_LoadDemoPattern(uint8_t phase);         // 加载演示图案

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
  * @brief  加载演示图案到LED矩阵
  * @param  phase: 演示相位（0-15循环）
  * @retval None
  * 
  * 此函数创建一个流动的光效，从外侧向内侧移动。
  * 每个LED根据其位置和当前相位计算亮度值。
  */
static void LED_LoadDemoPattern(uint8_t phase)
{
  uint32_t chain;   // 灯带索引
  uint32_t led;     // LED索引
  uint8_t step;     // 当前步进值

  // 提取相位的低4位作为步进值（0-15循环）
  step = (uint8_t)(phase & 0x0FU);
  
  // 遍历每条灯带上的所有LED
  for (led = 0U; led < LEDS_PER_CHAIN; led++)
  {
    uint8_t distance;   // LED距离参考点的距离
    uint8_t delta;      // 距离与步进的差值
    uint8_t brightness; // 计算出的亮度值

    /* 按每条总线的灯序流动：led=0 视为最外侧，led=15 视为最内侧 */
    distance = (uint8_t)led;
    // 计算当前位置与流动前沿的相对距离（模16运算）
    delta = (uint8_t)((distance + 16U - step) & 0x0FU);

    // 根据相对距离设置不同的亮度级别
    if (delta == 0U)
    {
      brightness = 255U;  // 最亮（白色）
    }
    else if (delta == 1U)
    {
      brightness = 96U;   // 中等亮度
    }
    else if (delta == 2U)
    {
      brightness = 32U;   // 较暗
    }
    else
    {
      brightness = 0U;    // 关闭
    }

    // 将相同的颜色值应用到所有32条灯带的对应LED位置
    for (chain = 0U; chain < LED_CHAIN_COUNT; chain++)
    {
      LED_Matrix_SetPixel(chain, led, ((uint32_t)brightness << 16U) | 
                                    ((uint32_t)brightness << 8U) | 
                                    (uint32_t)brightness);
    }
  }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  // 程序入口点开始

  /* USER CODE BEGIN 1 */
  // 用户代码区域1：在系统初始化之前可以添加的用户代码
  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  // 配置内存保护单元(MPU)，用于设置内存访问权限和属性
  MPU_Config();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  // 初始化HAL库，重置所有外设，初始化Flash接口和系统滴答定时器
  HAL_Init();

  /* USER CODE BEGIN Init */
  // 用户初始化代码区域：在系统时钟配置之前可以添加的用户代码
  /* USER CODE END Init */

  /* Configure the system clock */
  // 配置系统时钟，设置CPU、AHB、APB总线的时钟频率
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  // 用户系统初始化代码区域：在外设初始化之前可以添加的用户代码
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  // 初始化所有已配置的外设
  MX_GPIO_Init();    // 初始化GPIO引脚配置
  MX_DMA_Init();     // 初始化DMA控制器配置
  MX_TIM3_Init();    // 初始化定时器3配置（用于LED矩阵数据传输）
  MX_TIM2_Init();    // 初始化定时器2配置（用于帧刷新定时）
  MX_TIM8_Init();    // 初始化定时器8配置（用于LED矩阵数据传输）
  MX_SPI1_Init();    // 初始化SPI1接口配置
  /* USER CODE BEGIN 2 */
  // 配置定时器3的DMA传输完成回调函数
  hdma_tim3_up.XferCpltCallback = LED_Matrix_OnTim3DmaComplete;
  // 配置定时器3的DMA传输错误回调函数
  hdma_tim3_up.XferErrorCallback = LED_Matrix_OnDmaError;
  // 配置定时器8的DMA传输完成回调函数
  hdma_tim8_up.XferCpltCallback = LED_Matrix_OnTim8DmaComplete;
  // 配置定时器8的DMA传输错误回调函数
  hdma_tim8_up.XferErrorCallback = LED_Matrix_OnDmaError;
  
  // 初始化LED矩阵驱动
  LED_Matrix_Init();

  // 启动定时器2的中断模式，用于定期触发LED帧刷新
  if (HAL_TIM_Base_Start_IT(&htim2) != HAL_OK)
  {
    // 如果启动失败，调用错误处理函数
    Error_Handler();
  }

  // 启动第一帧LED显示
  LED_LoadDemoPattern(g_demo_phase++);
  LED_Matrix_StartFrame();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  // 主程序无限循环
  while (1)
  {
    // 进入等待中断模式，降低功耗，等待定时器中断唤醒
    __WFI();
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

/**
  * @brief  HAL定时器周期结束回调函数
  * @param  htim: 定时器句柄指针
  * @retval None
  * 
  * 此函数在定时器中断中被调用。当TIM2计数溢出时，
  * 触发新的LED帧刷新，实现持续的动画效果。
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM2)
  {
    // TIM2用于帧刷新定时，每到周期就启动新帧
    LED_LoadDemoPattern(g_demo_phase++);
    LED_Matrix_StartFrame();
  }
}

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
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
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  // 禁用所有中断
  __disable_irq();
  // 进入死循环（错误状态）
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
