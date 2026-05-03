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
#include "stm32h7xx.h"
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
#define LED_CHAIN_COUNT              32U
#define LEDS_PER_CHAIN               16U
#define LED_BITS_PER_PIXEL           24U
#define LED_SLOTS_PER_BIT            4U
#define LED_RESET_TIME_US            280U
#define LED_SLOT_TIME_NS             312U
#define LED_BRIGHTNESS_SHIFT         3U /* right shift per color component: 0=full,1=1/2,2=1/4,3=1/8 */
#define LED_RESET_SLOTS              ((LED_RESET_TIME_US * 1000U + LED_SLOT_TIME_NS - 1U) / LED_SLOT_TIME_NS)
#define LED_FRAME_SLOTS              ((LEDS_PER_CHAIN * LED_BITS_PER_PIXEL * LED_SLOTS_PER_BIT) + LED_RESET_SLOTS)
#define LED_FRAME_BYTES              (LED_FRAME_SLOTS * sizeof(uint16_t))
#define LED_FRAME_CACHE_BYTES        ((LED_FRAME_BYTES + 31U) & ~31U)

static uint8_t g_tim3_frame_storage[LED_FRAME_CACHE_BYTES + 31U];
static uint8_t g_tim8_frame_storage[LED_FRAME_CACHE_BYTES + 31U];
static uint16_t *g_tim3_frame = (uint16_t *)0;
static uint16_t *g_tim8_frame = (uint16_t *)0;
static uint32_t g_led_grb[LED_CHAIN_COUNT][LEDS_PER_CHAIN];
static volatile uint8_t g_frame_busy = 0U;
static volatile uint8_t g_tim3_done = 0U;
static volatile uint8_t g_tim8_done = 0U;
static uint8_t g_demo_phase = 0U;
extern DMA_HandleTypeDef hdma_tim3_up;
extern DMA_HandleTypeDef hdma_tim8_up;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
/* USER CODE BEGIN PFP */
static void LED_LoadDemoPattern(uint8_t phase);
static void LED_InitFrameBuffers(void);
static void LED_BuildFrame(void);
static void LED_CleanFrameCache(void);
static void LED_StartFrame(void);
static void LED_StopFrame(void);
static void LED_OnTim3DmaComplete(DMA_HandleTypeDef *hdma);
static void LED_OnTim8DmaComplete(DMA_HandleTypeDef *hdma);
static void LED_OnDmaError(DMA_HandleTypeDef *hdma);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static void LED_LoadDemoPattern(uint8_t phase)
{
  uint32_t chain;
  uint32_t led;

  for (chain = 0U; chain < LED_CHAIN_COUNT; chain++)
  {
    for (led = 0U; led < LEDS_PER_CHAIN; led++)
    {
      uint8_t wave = (uint8_t)((chain * 7U) + (led * 13U) + phase * 11U);
      uint8_t green = wave;
      uint8_t red = (uint8_t)(255U - wave);
      uint8_t blue = (uint8_t)((chain * 9U) ^ (led * 17U) ^ phase);
      g_led_grb[chain][led] = ((uint32_t)green << 16) | ((uint32_t)red << 8) | (uint32_t)blue;
    }
  }
}

static void LED_InitFrameBuffers(void)
{
  uintptr_t tim3_base;
  uintptr_t tim8_base;

  tim3_base = (uintptr_t)g_tim3_frame_storage;
  tim8_base = (uintptr_t)g_tim8_frame_storage;
  tim3_base = (tim3_base + 31U) & ~(uintptr_t)31U;
  tim8_base = (tim8_base + 31U) & ~(uintptr_t)31U;

  g_tim3_frame = (uint16_t *)tim3_base;
  g_tim8_frame = (uint16_t *)tim8_base;
}

static void LED_BuildFrame(void)
{
  uint32_t slotIndex = 0U;
  uint32_t ledIndex;
  uint32_t bitIndex;
  uint32_t chainIndex;

  for (ledIndex = 0U; ledIndex < LEDS_PER_CHAIN; ledIndex++)
  {
    for (bitIndex = 0U; bitIndex < LED_BITS_PER_PIXEL; bitIndex++)
    {
      uint16_t portD = 0xFFFFU;
      uint16_t portE = 0xFFFFU;

      g_tim3_frame[slotIndex] = portD;
      g_tim8_frame[slotIndex] = portE;
      slotIndex++;

      portD = 0U;
      portE = 0U;

          for (chainIndex = 0U; chainIndex < LED_CHAIN_COUNT; chainIndex++)
      {
            uint32_t v = g_led_grb[chainIndex][ledIndex];
            uint8_t g = (uint8_t)((v >> 16U) & 0xFFU);
            uint8_t r = (uint8_t)((v >> 8U) & 0xFFU);
            uint8_t b = (uint8_t)(v & 0xFFU);

            /* 应用全局亮度缩放（右移 LED_BRIGHTNESS_SHIFT 位） */
            g = (uint8_t)(g >> LED_BRIGHTNESS_SHIFT);
            r = (uint8_t)(r >> LED_BRIGHTNESS_SHIFT);
            b = (uint8_t)(b >> LED_BRIGHTNESS_SHIFT);

            uint32_t scaled = ((uint32_t)g << 16U) | ((uint32_t)r << 8U) | (uint32_t)b;

            if ((scaled & (1UL << (23U - bitIndex))) != 0UL)
            {
              if (chainIndex < 16U)
              {
                portD |= (uint16_t)(1U << chainIndex);
              }
              else
              {
                portE |= (uint16_t)(1U << (chainIndex - 16U));
              }
            }
      }

      g_tim3_frame[slotIndex] = portD;
      g_tim8_frame[slotIndex] = portE;
      slotIndex++;

      g_tim3_frame[slotIndex] = 0U;
      g_tim8_frame[slotIndex] = 0U;
      slotIndex++;

      g_tim3_frame[slotIndex] = 0U;
      g_tim8_frame[slotIndex] = 0U;
      slotIndex++;
    }
  }

  while (slotIndex < LED_FRAME_SLOTS)
  {
    g_tim3_frame[slotIndex] = 0U;
    g_tim8_frame[slotIndex] = 0U;
    slotIndex++;
  }
}

static void LED_CleanFrameCache(void)
{
}

static void LED_StopFrame(void)
{
  (void)HAL_TIM_Base_Stop(&htim3);
  (void)HAL_TIM_Base_Stop(&htim8);
  g_frame_busy = 0U;
}

static void LED_StartFrame(void)
{
  static int a = 0 ;
  a++ ;
  if (g_frame_busy != 0U)
  {
    return;
  }

  if ((g_tim3_frame == (uint16_t *)0) || (g_tim8_frame == (uint16_t *)0))
  {
    LED_InitFrameBuffers();
  }

  g_frame_busy = 1U;
  g_tim3_done = 0U;
  g_tim8_done = 0U;

  LED_LoadDemoPattern(g_demo_phase++);
  LED_BuildFrame();
  LED_CleanFrameCache();

  __HAL_TIM_SET_COUNTER(&htim3, 0U);
  __HAL_TIM_SET_COUNTER(&htim8, 0U);
  __HAL_TIM_CLEAR_FLAG(&htim3, TIM_FLAG_UPDATE);
  __HAL_TIM_CLEAR_FLAG(&htim8, TIM_FLAG_UPDATE);

  if (HAL_DMA_Start_IT(&hdma_tim3_up, (uint32_t)g_tim3_frame, (uint32_t)&GPIOD->ODR, LED_FRAME_SLOTS) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_DMA_Start_IT(&hdma_tim8_up, (uint32_t)g_tim8_frame, (uint32_t)&GPIOE->ODR, LED_FRAME_SLOTS) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_TIM_Base_Start(&htim3) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_TIM_Base_Start(&htim8) != HAL_OK)
  {
    Error_Handler();
  }
}

static void LED_OnTim3DmaComplete(DMA_HandleTypeDef *hdma)
{
  (void)hdma;
  g_tim3_done = 1U;
  if (g_tim8_done != 0U)
  {
    LED_StopFrame();
  }
}

static void LED_OnTim8DmaComplete(DMA_HandleTypeDef *hdma)
{
  (void)hdma;
  g_tim8_done = 1U;
  if (g_tim3_done != 0U)
  {
    LED_StopFrame();
  }
}

static void LED_OnDmaError(DMA_HandleTypeDef *hdma)
{
  (void)hdma;
  Error_Handler();
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
  MX_TIM2_Init();
  MX_TIM8_Init();
  MX_SPI1_Init();
  /* USER CODE BEGIN 2 */
  hdma_tim3_up.XferCpltCallback = LED_OnTim3DmaComplete;
  hdma_tim3_up.XferErrorCallback = LED_OnDmaError;
  hdma_tim8_up.XferCpltCallback = LED_OnTim8DmaComplete;
  hdma_tim8_up.XferErrorCallback = LED_OnDmaError;

  __HAL_TIM_ENABLE_DMA(&htim8, TIM_DMA_UPDATE);
  __HAL_TIM_ENABLE_DMA(&htim3, TIM_DMA_UPDATE);
  if (HAL_TIM_Base_Start_IT(&htim2) != HAL_OK)
  {
    Error_Handler();
  }

  LED_StartFrame();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
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

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM2)
  {
    LED_StartFrame();
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
