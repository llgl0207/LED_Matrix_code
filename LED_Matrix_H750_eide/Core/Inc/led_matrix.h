#ifndef __LED_MATRIX_H
#define __LED_MATRIX_H
#include "main.h"
#include "dma.h"
#include "tim.h"
#include "gpio.h"
  extern DMA_HandleTypeDef hdma_tim3_up;
  extern DMA_HandleTypeDef hdma_tim8_up;
#define ONE_BUS_LED_NUM 16
#define DMA_BUFFER_CYLINDER_NUM 32//直接用于DMA输出的缓冲区，因为协议要求占据原始缓冲区的四倍大小
#define RAW_BUFFER_CYLINDER_NUM 64//存储原始帧数据
#define RGB_PROTOCOL 1
//0: RGB协议，1: GRB协议
#define BRIGHT_SHIFT 4 // 亮度调整位数，0~7，数值越大亮度越暗
typedef struct {
    uint16_t ledBufferRawA[ONE_BUS_LED_NUM][24];//LED矩阵有两个半柱面，所以需要AB。共有GPIOD和E分别输出两个半柱面，一开始DE分别输出AB柱面，转半圈后DE分别输出BA柱面
    uint16_t ledBufferRawB[ONE_BUS_LED_NUM][24];
} memFrameRaw;//存储原始数据的帧缓存，经过协议转换后写入FrameDma用于DMA输出
extern memFrameRaw FrameRaw[RAW_BUFFER_CYLINDER_NUM];
typedef struct {
    uint16_t ledBufferDmaA[ONE_BUS_LED_NUM][24*4];
    uint16_t ledBufferDmaB[ONE_BUS_LED_NUM][24*4];
} memFrameDma;//用于DMA的帧缓存，直接用于输出

extern memFrameDma FrameDmaA[DMA_BUFFER_CYLINDER_NUM];//这里的AB不是指两个柱面，而是相邻的双缓冲区
extern memFrameDma FrameDmaB[DMA_BUFFER_CYLINDER_NUM];

void ledSetColorOneRaw(uint16_t bufferRaw[ONE_BUS_LED_NUM][24], uint8_t ledSeq, uint8_t ioSeq, uint32_t color);
void ledBufferClearRaw(uint16_t bufferRaw[ONE_BUS_LED_NUM][24]);
void ledSetColorOneDma(uint16_t bufferDma[ONE_BUS_LED_NUM][24*4], uint8_t ledSeq, uint8_t ioSeq, uint32_t color);
void ledBufferClearDma(uint16_t bufferDma[ONE_BUS_LED_NUM][24*4]);
void ledBufferRawToDma(memFrameDma * frameDma, memFrameRaw * frameRaw);
void ledBufferInit(void);
void ledPushGPIO(memFrameDma * frame, uint8_t mode);
void ledPushGPIOVolume(memFrameDma * frame);
#endif