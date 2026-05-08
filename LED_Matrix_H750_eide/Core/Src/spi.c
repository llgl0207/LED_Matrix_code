/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    spi.c
  * @brief   This file provides code for the configuration
  *          of the SPI instances.
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
#include "spi.h"

/* USER CODE BEGIN 0 */

#include <string.h>
#include "led_matrix.h"

#define SPI_FRAME_MAGIC 0x4C45444DUL
#define SPI_CMD_PREPARE 0x00000001UL
#define SPI_CMD_STATUS  0x00000002UL

#define SPI_STATUS_READY 0x00000001UL
#define SPI_STATUS_BUSY  0x00000002UL
#define SPI_STATUS_DONE  0x00000003UL
#define SPI_STATUS_ERROR 0x000000FFUL

typedef enum {
  SPI_STATE_CMD = 0,
  SPI_STATE_DATA = 1
} SpiFrameState;

static volatile uint32_t g_spiStatus = SPI_STATUS_READY;
static volatile SpiFrameState g_spiState = SPI_STATE_CMD;
static uint32_t g_cmdRx[3];
static uint32_t g_cmdTx[3];
static uint32_t g_expectedWords = 0;
static memFrameRaw *g_fillRaw = NULL;
static uint32_t g_dataStartTick = 0;
static uint32_t g_dataTimeoutMs = 200;
static uint32_t g_expectedWords = 0;
static memFrameRaw *g_fillRaw = NULL;
static uint32_t g_dataStartTick = 0;
static uint32_t g_dataTimeoutMs = 200;

#define DCACHE_LINE_SIZE 32U

static void spi_invalidate_dcache(void *addr, size_t size){
  uintptr_t start = (uintptr_t)addr;
  uintptr_t end = start + size;
  start &= ~(DCACHE_LINE_SIZE - 1U);
  end = (end + (DCACHE_LINE_SIZE - 1U)) & ~(DCACHE_LINE_SIZE - 1U);
  SCB_InvalidateDCache_by_Addr((uint32_t*)start, (int32_t)(end - start));
}

volatile uint32_t g_spiDbgState = 0;
volatile uint32_t g_spiDbgLastMagic = 0;
volatile uint32_t g_spiDbgLastCmd = 0;
volatile uint32_t g_spiDbgLastLen = 0;
volatile uint32_t g_spiDbgLastRxStatus = 0;

static uint32_t spi_frame_words(void){
  return (RAW_BUFFER_CYLINDER_NUM * sizeof(memFrameRaw)) / sizeof(uint32_t);
}

static void spi_prepare_cmd_rx(void){
  g_cmdTx[0] = g_spiStatus;
  g_cmdTx[1] = g_spiStatus;
  g_cmdTx[2] = g_spiStatus;
  HAL_SPI_TransmitReceive_IT(&hspi1, (uint8_t*)g_cmdTx, (uint8_t*)g_cmdRx, 3);
}
/* USER CODE END 0 */

SPI_HandleTypeDef hspi1;
DMA_HandleTypeDef hdma_spi1_rx;
DMA_HandleTypeDef hdma_spi1_tx;

/* SPI1 init function */
void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_SLAVE;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_32BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_HARD_INPUT;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 0x0;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  hspi1.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
  hspi1.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
  hspi1.Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  hspi1.Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  hspi1.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
  hspi1.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  hspi1.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  hspi1.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
  hspi1.Init.IOSwap = SPI_IO_SWAP_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

void HAL_SPI_MspInit(SPI_HandleTypeDef* spiHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(spiHandle->Instance==SPI1)
  {
  /* USER CODE BEGIN SPI1_MspInit 0 */

  /* USER CODE END SPI1_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SPI1;
    PeriphClkInitStruct.Spi123ClockSelection = RCC_SPI123CLKSOURCE_PLL;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    /* SPI1 clock enable */
    __HAL_RCC_SPI1_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**SPI1 GPIO Configuration
    PA4     ------> SPI1_NSS
    PA5     ------> SPI1_SCK
    PA6     ------> SPI1_MISO
    PA7     ------> SPI1_MOSI
    */
    GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* SPI1 DMA Init */
    /* SPI1_RX Init */
    hdma_spi1_rx.Instance = DMA1_Stream1;
    hdma_spi1_rx.Init.Request = DMA_REQUEST_SPI1_RX;
    hdma_spi1_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_spi1_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_spi1_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_spi1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_spi1_rx.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hdma_spi1_rx.Init.Mode = DMA_NORMAL;
    hdma_spi1_rx.Init.Priority = DMA_PRIORITY_HIGH;
    hdma_spi1_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_spi1_rx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(spiHandle,hdmarx,hdma_spi1_rx);

    /* SPI1_TX Init */
    hdma_spi1_tx.Instance = DMA1_Stream3;
    hdma_spi1_tx.Init.Request = DMA_REQUEST_SPI1_TX;
    hdma_spi1_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_spi1_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_spi1_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_spi1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_spi1_tx.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hdma_spi1_tx.Init.Mode = DMA_NORMAL;
    hdma_spi1_tx.Init.Priority = DMA_PRIORITY_HIGH;
    hdma_spi1_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_spi1_tx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(spiHandle,hdmatx,hdma_spi1_tx);

    /* SPI1 interrupt Init */
    HAL_NVIC_SetPriority(SPI1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(SPI1_IRQn);
  /* USER CODE BEGIN SPI1_MspInit 1 */

  /* USER CODE END SPI1_MspInit 1 */
  }
}

void HAL_SPI_MspDeInit(SPI_HandleTypeDef* spiHandle)
{

  if(spiHandle->Instance==SPI1)
  {
  /* USER CODE BEGIN SPI1_MspDeInit 0 */

  /* USER CODE END SPI1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_SPI1_CLK_DISABLE();

    /**SPI1 GPIO Configuration
    PA4     ------> SPI1_NSS
    PA5     ------> SPI1_SCK
    PA6     ------> SPI1_MISO
    PA7     ------> SPI1_MOSI
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7);

    /* SPI1 DMA DeInit */
    HAL_DMA_DeInit(spiHandle->hdmarx);
    HAL_DMA_DeInit(spiHandle->hdmatx);

    /* SPI1 interrupt Deinit */
    HAL_NVIC_DisableIRQ(SPI1_IRQn);
  /* USER CODE BEGIN SPI1_MspDeInit 1 */

  /* USER CODE END SPI1_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

void SPI_FrameInit(void){
  g_spiStatus = SPI_STATUS_READY;
  g_spiState = SPI_STATE_CMD;
  g_expectedWords = spi_frame_words();
  g_spiDbgState = 1;
  spi_prepare_cmd_rx();
}

void SPI_FramePoll(void){
  if(g_spiState != SPI_STATE_DATA){
    return;
  }
  uint32_t now = HAL_GetTick();
  if((now - g_dataStartTick) > g_dataTimeoutMs){
    HAL_SPI_Abort(&hspi1);
    g_spiStatus = SPI_STATUS_ERROR;
    g_spiState = SPI_STATE_CMD;
    g_spiDbgState = 16;
    spi_prepare_cmd_rx();
  }
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi){
  if(hspi->Instance != SPI1){
    return;
  }
  if(g_spiState != SPI_STATE_CMD){
    return;
  }

  spi_invalidate_dcache(g_fillRaw, RAW_BUFFER_CYLINDER_NUM * sizeof(memFrameRaw));
  uint32_t magic = g_cmdRx[0];
  uint32_t cmd = g_cmdRx[1];
  uint32_t lengthWords = g_cmdRx[2];
  g_spiDbgLastMagic = magic;
  g_spiDbgLastCmd = cmd;
  g_spiDbgLastLen = lengthWords;
  g_spiDbgState = 2;

  if(magic != SPI_FRAME_MAGIC){
    g_spiStatus = SPI_STATUS_ERROR;
    g_spiDbgState = 10;
    spi_prepare_cmd_rx();
    return;
  }

  if(cmd == SPI_CMD_PREPARE){
    if(g_spiStatus == SPI_STATUS_BUSY){
      g_spiDbgState = 11;
      spi_prepare_cmd_rx();
      return;
    }
    if(lengthWords != g_expectedWords){
      g_spiStatus = SPI_STATUS_ERROR;
      g_spiDbgState = 12;
      spi_prepare_cmd_rx();
      return;
    }
    g_spiStatus = SPI_STATUS_BUSY;
    g_spiState = SPI_STATE_DATA;
    g_fillRaw = ledGetFillRaw();
    g_dataStartTick = HAL_GetTick();
    g_spiDbgState = 3;
    if (HAL_SPI_Receive_DMA(&hspi1, (uint8_t*)g_fillRaw, g_expectedWords) != HAL_OK) {
      g_spiStatus = SPI_STATUS_ERROR;
      g_spiState = SPI_STATE_CMD;
      g_spiDbgState = 13;
      spi_prepare_cmd_rx();
    }
    return;
  }

  if(cmd == SPI_CMD_STATUS){
    g_spiDbgLastRxStatus = g_spiStatus;
    g_spiDbgState = 4;
    if(g_spiStatus == SPI_STATUS_DONE){
      g_spiStatus = SPI_STATUS_READY;
    }
    spi_prepare_cmd_rx();
    return;
  }

  g_spiStatus = SPI_STATUS_ERROR;
  g_spiDbgState = 14;
  spi_prepare_cmd_rx();
}

void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi){
  if(hspi->Instance != SPI1){
    return;
  }
  if(g_spiState != SPI_STATE_DATA){
    return;
  }

  ledSwapRawBuffers();
  g_spiDbgState = 5;
  g_spiStatus = SPI_STATUS_DONE;
  g_spiState = SPI_STATE_CMD;
  spi_prepare_cmd_rx();
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi){
  if(hspi->Instance != SPI1){
    return;
  }
  g_spiStatus = SPI_STATUS_ERROR;
  g_spiDbgState = 15;
  g_spiState = SPI_STATE_CMD;
  spi_prepare_cmd_rx();
}

/* USER CODE END 1 */

