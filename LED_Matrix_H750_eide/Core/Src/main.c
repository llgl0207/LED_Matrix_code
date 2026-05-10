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
#include "dma.h"
#include "spi.h"
#include "tim.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "tim.h"
#include "led_matrix.h"
#include <math.h>
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
#define FRAME_COUNT (RAW_BUFFER_CYLINDER_NUM * 2)  // 动画总帧数（64帧完成一圈）

#define SPI_SLAVE_FRAME_COUNT 16
static uint16_t spiSlaveRxBuf[SPI_SLAVE_FRAME_COUNT];
static uint16_t spiSlaveTxBuf[SPI_SLAVE_FRAME_COUNT] = {
  0xB001, 0xB002, 0xB003, 0xB004,
  0xB005, 0xB006, 0xB007, 0xB008,
  0xB009, 0xB00A, 0xB00B, 0xB00C,
  0xB00D, 0xB00E, 0xB00F, 0xB010
};
static volatile uint8_t spiTransferComplete = 0;

// 用于计算平均值的历史数据
#define SAMPLE_COUNT 10          // 取前50次的平均值
uint32_t intervalHistory[50]; // 存储最近50次中断间隔
uint8_t historyIndex = 0;        // 当前存储位置索引
uint8_t sampleCount = 0;         // 当前已有样本数量

typedef enum {
  PATTERN_COLOR = 0,
  PATTERN_CYLINDER = 1,
  PATTERN_CONE = 2,
  PATTERN_CUBE = 3
} PatternType;

static PatternType g_pattern = PATTERN_CUBE;
static const float kHeightToWidth = 2.0f;
static const PatternType kPatternCycle[] = {PATTERN_CYLINDER, PATTERN_CONE, PATTERN_CUBE};
static uint8_t g_patternIndex = 0;
static uint32_t g_lastPatternTick = 0;
static uint32_t g_lastFrameTick = 0;
static float g_cubeAngle = 0.0f;
static const float kCubeAngleStep = 0.1745329f;

/*
注意，当前的显示器设计是RAW_BUFFER_CYLINDER_NUM个RAW切片，每个切片内包含两个数组，分别在各自对面，
这个体积显示器是对称的两个半柱面，所以每个RAW切片的数据会被转换成两个DMA切片，分别输出到GPIOD和GPIOE。
所有RAW切片遍历完一次后，显示器其实只转过了半圈，这时就要调换RAW切片下的AB输出方向，将D和E对调，重新输出。
此外，GPIO下的16个IO对应的是从上到下的16行，每一行里的16个灯对应的是从外到内的16列，所以数组末尾的灯是最内侧的灯。
特别提示，当前每一个像素都是等距分布，也就是说，两个柱面会把图像拉伸。例如要显示正方形，每个半柱面的高是宽的两倍

【重要补充说明】
- 3D空间坐标系统：每个LED点由(frameIdx, ledPos, io)三元组确定
  * frameIdx (0-63): 角度方向，对应圆周360度位置
  * ledPos (0-15): 半径方向，0=最外侧，15=最内侧  
  * io (0-15): 高度方向，0=顶部，15=底部
- 3D渲染原理：通过判断每个(frameIdx, ledPos, io)对应的3D空间点(x,y,z)是否在目标几何体内
  * x = r * cos(theta), y = r * sin(theta), 其中r = 1.0 - ledPos/15.0, theta = π * frameIdx / RAW_BUFFER_CYLINDER_NUM
  * z = 1.0 - 2.0 * io/15.0 (高度方向，考虑高宽比kHeightToWidth=2.0)
- 渲染模式：PATTERN_CUBE等模式通过ledBuildPatternFrames()函数实现，该函数填充fillRaw缓冲区后调用ledSwapRawBuffers()进行双缓冲交换
- 颜色控制：原PATTERN_CUBE使用ledRainbowColor()生成动态彩色效果，静止立方体可使用固定颜色(如0xFFFFFF)
*/
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
void MX_FREERTOS_Init(void);
void StartSpiSlaveDma(void);
/* USER CODE BEGIN PFP */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static uint32_t ledRainbowColor(float phase){
  float r = sinf(phase) * 0.5f + 0.5f;
  float g = sinf(phase + 2.0943951f) * 0.5f + 0.5f;
  float b = sinf(phase + 4.1887902f) * 0.5f + 0.5f;

  uint8_t red = (uint8_t)(r * 255.0f);
  uint8_t green = (uint8_t)(g * 255.0f);
  uint8_t blue = (uint8_t)(b * 255.0f);

  return ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue;
}

static void ledBuildPatternFrames(PatternType pattern){
  memFrameRaw *fillRaw = ledGetFillRaw();

  for (int frameIdx = 0; frameIdx < RAW_BUFFER_CYLINDER_NUM; frameIdx++) {
    ledBufferClearRaw(fillRaw[frameIdx].ledBufferRawA);
    ledBufferClearRaw(fillRaw[frameIdx].ledBufferRawB);

    switch (pattern) {
      case PATTERN_COLOR: {
        uint32_t colors[8] = {
          0xFF0000,
          0x00FF00,
          0x0000FF,
          0xFFFF00,
          0xFF00FF,
          0x00FFFF,
          0xFF8800,
          0xFF88FF
        };

        int colorIdx = (frameIdx * 8) / RAW_BUFFER_CYLINDER_NUM;
        uint32_t colorA = colors[colorIdx % 8];
        uint32_t colorB = colors[(colorIdx + 4) % 8];

        for (int ledPos = 0; ledPos < ONE_BUS_LED_NUM; ledPos++) {
          for (int io = 0; io < 16; io++) {
            ledSetColorOneRaw(fillRaw[frameIdx].ledBufferRawA, ledPos, io, colorA);
            ledSetColorOneRaw(fillRaw[frameIdx].ledBufferRawB, ledPos, io, colorB);
          }
        }
        break;
      }
      case PATTERN_CYLINDER: {
        for (int ledPos = 0; ledPos < ONE_BUS_LED_NUM; ledPos++) {
          for (int io = 0; io < 16; io++) {
            ledSetColorOneRaw(fillRaw[frameIdx].ledBufferRawA, ledPos, io, 0xFFFFFF);
            ledSetColorOneRaw(fillRaw[frameIdx].ledBufferRawB, ledPos, io, 0xFFFFFF);
          }
        }
        break;
      }
      case PATTERN_CONE: {
        for (int io = 0; io < 16; io++) {
          float z = 1.0f - (2.0f * ((float)io / 15.0f));
          float zScaled = z / kHeightToWidth;
          float t = (zScaled + 1.0f) * 0.5f;
          if (t < 0.0f) {
            t = 0.0f;
          }
          if (t > 1.0f) {
            t = 1.0f;
          }
          int radiusCount = (int)((t * (float)ONE_BUS_LED_NUM) + 0.5f);
          int start = ONE_BUS_LED_NUM - radiusCount;
          if (start < 0) {
            start = 0;
          }
          for (int ledPos = start; ledPos < ONE_BUS_LED_NUM; ledPos++) {
            ledSetColorOneRaw(fillRaw[frameIdx].ledBufferRawA, ledPos, io, 0xFFFFFF);
            ledSetColorOneRaw(fillRaw[frameIdx].ledBufferRawB, ledPos, io, 0xFFFFFF);
          }
        }
        break;
      }
      case PATTERN_CUBE: {
        const float cubeHalf = 0.75f;
        float theta = g_cubeAngle + (3.1415926f * (float)frameIdx) / (float)RAW_BUFFER_CYLINDER_NUM;
        float c = cosf(theta);
        float s = sinf(theta);
        for (int ledPos = 0; ledPos < ONE_BUS_LED_NUM; ledPos++) {
          float r = 1.0f - ((float)ledPos / 15.0f);
          float x = r * c;
          float y = r * s;
          float ax = fabsf(x);
          float ay = fabsf(y);
          for (int io = 0; io < 16; io++) {
            float z = 1.0f - (2.0f * ((float)io / 15.0f));
            float az = fabsf(z) / kHeightToWidth;
            if ((ax <= cubeHalf) && (ay <= cubeHalf) && (az <= cubeHalf)) {
              float phase = g_cubeAngle + (float)frameIdx * 0.12f + (float)ledPos * 0.35f + (float)io * 0.22f;
              uint32_t color = ledRainbowColor(phase);
              ledSetColorOneRaw(fillRaw[frameIdx].ledBufferRawA, ledPos, io, color);
              ledSetColorOneRaw(fillRaw[frameIdx].ledBufferRawB, ledPos, io, color);
            }
          }
        }
        break;
      }
      default:
        break;
    }
  }

  ledSwapRawBuffers();

  memFrameRaw *renderRaw = ledGetRenderRaw();
  for (int i = 0; i < DMA_BUFFER_CYLINDER_NUM; i++) {
    ledBufferClearDma(FrameDmaA[i].ledBufferDmaA);
    ledBufferClearDma(FrameDmaA[i].ledBufferDmaB);
    ledBufferRawToDma(&FrameDmaA[i], &renderRaw[i]);

    ledBufferClearDma(FrameDmaB[i].ledBufferDmaA);
    ledBufferClearDma(FrameDmaB[i].ledBufferDmaB);
    ledBufferRawToDma(&FrameDmaB[i], &renderRaw[i + DMA_BUFFER_CYLINDER_NUM]);
  }
}

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
  // MX_TIM2_Init();
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
  // HAL_TIM_Base_Start_IT(&htim2);
  
  // 使能定时器DMA请求（必须）
  __HAL_TIM_ENABLE_DMA(&htim3, TIM_DMA_UPDATE);
  __HAL_TIM_ENABLE_DMA(&htim8, TIM_DMA_UPDATE);
  
  // 初始化LED缓冲区
  ledBufferInit();
  // ledBuildPatternFrames(g_pattern);
  // g_lastFrameTick = HAL_GetTick();

  StartSpiSlaveDma();
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
        } else
        {
          // 如果是第一次触发，则将当前间隔存入历史数组
          intervalHistory[historyIndex] = currentInterval;
          historyIndex = (historyIndex + 1) % SAMPLE_COUNT;
          
          // 初始化样本数量为1
          sampleCount = 1;
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
      __HAL_TIM_SET_AUTORELOAD(&htim2, newArr);
      
      // 重置动画状态，立即重新开始渲染
      animationFrame = 0;
      isRefreshing = 1;
    }
  }
}

void StartRenderDma(void *argument){
  for(;;){
    //     uint32_t now = HAL_GetTick();
    // if ((now - g_lastFrameTick) >= 50) {
    //   g_pattern = PATTERN_CUBE;
    //   ledBuildPatternFrames(g_pattern);
    //   g_lastFrameTick = now;
    // }

    // 后台任务：动态加载Raw帧到DMA缓冲区
    if (isRefreshing) {
      // 计算需要预加载的帧索引（领先当前显示帧2*DMA_BUFFER_CYLINDER_NUM）
      int preloadFrame = (animationFrame + 2 * DMA_BUFFER_CYLINDER_NUM) % (RAW_BUFFER_CYLINDER_NUM * 2);
      // 转换为Raw帧索引（0-RAW_BUFFER_CYLINDER_NUM-1）
      int rawFrameIdx = preloadFrame % RAW_BUFFER_CYLINDER_NUM;
      // 计算目标DMA缓冲区索引
      int dmaIdx = rawFrameIdx % DMA_BUFFER_CYLINDER_NUM;
      
      // 确定目标缓冲区（与当前使用的缓冲区相反）
      memFrameDma *targetDma;
      if (rawFrameIdx < DMA_BUFFER_CYLINDER_NUM) {
        targetDma = &FrameDmaA[dmaIdx];
      } else {
        targetDma = &FrameDmaB[dmaIdx];
      }
      
      memFrameRaw *currentRenderRaw = ledGetRenderRaw();
      // 从Raw缓冲区转换到DMA缓冲区
      ledBufferClearDma(targetDma->ledBufferDmaA);
      ledBufferClearDma(targetDma->ledBufferDmaB);
      ledBufferRawToDma(targetDma, &currentRenderRaw[rawFrameIdx]);
    }
    osDelay(10);
  }
}

void StartUiLogic(void *argument){
  for(;;){
    if (spiTransferComplete)
    {
      spiTransferComplete = 0;
      /* 处理接收到的 SPI 数据 */
      /* 例如：对比 spiSlaveRxBuf 内容、触发状态机、写入日志等 */
      StartSpiSlaveDma();
    }
    osDelay(100);
  }
}

void StartSpiSlaveDma(void)
{
  if (HAL_SPI_TransmitReceive_DMA(&hspi1, (uint8_t *)spiSlaveTxBuf, (uint8_t *)spiSlaveRxBuf, SPI_SLAVE_FRAME_COUNT) != HAL_OK)
  {
    Error_Handler();
  }
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
  if (hspi->Instance == SPI1)
  {
    spiTransferComplete = 1;
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
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM1 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM1)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */
  // if (htim->Instance == TIM2)
  // {
  //   // TIM2 LED 渲染逻辑已禁用，用于排查 SPI DMA 干扰。
  // }
  /* USER CODE END Callback 1 */
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
