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
  extern DMA_HandleTypeDef hdma_tim3_up;
  extern DMA_HandleTypeDef hdma_tim8_up;
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
uint8_t currentFrame = 0;       // 当前显示的帧索引 (0-119)
uint8_t refreshFlag = 0;        // 刷新标志，中断触发时设置为1
uint8_t isRefreshing = 0;       // 正在刷新中标志
uint32_t lastIrqTime = 0;       // 上次中断触发时间戳（用于20ms屏蔽）
#define DEBOUNCE_TIME 20         // 触发后屏蔽时间（毫秒）
uint8_t animationFrame = 0;     // 动画帧计数器，用于逐帧显示
uint32_t lastTriggerTime = 0;   // 上次有效中断触发时间（用于计算间隔）
uint32_t irqInterval = 1000;    // 两次中断的时间间隔（毫秒），初始1秒
#define TIM2_CLOCK 1000000       // TIM2经过PSC后的时钟频率（1MHz）
#define FRAME_COUNT 120          // 动画总帧数

// 用于计算平均值的历史数据
#define SAMPLE_COUNT 50          // 取前50次的平均值
uint32_t intervalHistory[50]; // 存储最近50次中断间隔
uint8_t historyIndex = 0;        // 当前存储位置索引
uint8_t sampleCount = 0;         // 当前已有样本数量
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
  HAL_TIM_Base_Start_IT(&htim2);
  
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
  for (int frameIdx = 0; frameIdx < 64; frameIdx++) {
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

    // 待机状态：可以添加低功耗处理或其他任务
    HAL_Delay(10);
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

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == IR_IT_Pin)
  {
    // 获取当前时间戳
    uint32_t currentTime = HAL_GetTick();
    
    // 检查是否在20ms屏蔽期内（上次触发后20ms内忽略新触发）
    // 如果是第一次触发（lastIrqTime为0），则立即处理
    if (lastIrqTime == 0 || (currentTime - lastIrqTime) >= DEBOUNCE_TIME)
    {
      // 更新上次触发时间（用于20ms屏蔽期）
      lastIrqTime = currentTime;
      
      // 计算与上一次有效触发的时间间隔
      if (lastTriggerTime != 0)
      {
        uint32_t currentInterval = currentTime - lastTriggerTime;
        
        // 将当前间隔存入历史数组（环形缓冲区）
        intervalHistory[historyIndex] = currentInterval;
        historyIndex = (historyIndex + 1) % SAMPLE_COUNT;
        
        // 更新样本数量（最多SAMPLE_COUNT个）
        if (sampleCount < SAMPLE_COUNT)
        {
          sampleCount++;
        }
        
        // 计算前N次的平均值
        uint32_t sum = 0;
        for (uint8_t i = 0; i < sampleCount; i++)
        {
          sum += intervalHistory[i];
        }
        irqInterval = sum / sampleCount;
      }
      
      // 更新上一次有效触发时间
      lastTriggerTime = currentTime;
      
      // 计算新的TIM2 ARR值，使动画在平均间隔时间内完成
      // ARR = (平均间隔时间(ms) * 1000) / FRAME_COUNT - 1
      // 确保ARR至少为1（避免除零或负数）
      uint32_t newArr = (irqInterval * 1000) / FRAME_COUNT;
      if (newArr < 1) newArr = 1;
      newArr -= 1; // TIM计数器从0开始，所以减1
      
      // 更新TIM2的ARR值
      __HAL_TIM_SET_AUTORELOAD(&htim2, 500);
      
      // 重置动画状态，立即重新开始渲染
      animationFrame = 0;
      isRefreshing = 1;
    }
  }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM2)
  {
    // 检查是否正在刷新动画
    if (isRefreshing)
    {
      // 每次中断只显示一帧，避免长时间占用中断
      int memIdx = animationFrame / 2;
      uint8_t isBufferA = (animationFrame % 2 == 0);
      
      // 启动DMA传输显示当前帧
      // 偶数帧：刷新bufferA到GPIOD
      // 奇数帧：刷新bufferB到GPIOE
      if (isBufferA) {
        HAL_DMA_Start_IT(&hdma_tim3_up, (uint32_t)displayMem[memIdx].ledBufferA, (uint32_t)&GPIOD->ODR, ONE_BUS_LED_NUM*24*4);
      } else {
        HAL_DMA_Start_IT(&hdma_tim8_up, (uint32_t)displayMem[memIdx].ledBufferB, (uint32_t)&GPIOE->ODR, ONE_BUS_LED_NUM*24*4);
      }
      
      // 更新动画帧计数器
      animationFrame++;
      
      // 检查是否播放完毕
      if (animationFrame >= CYLINDER_NUM)
      {
        // 刷新完成
        isRefreshing = 0;
        animationFrame = 0;
        currentFrame = 0;
      }
    }
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
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */