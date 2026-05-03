#ifndef __LED_MATRIX_H
#define __LED_MATRIX_H
#include "main.h"
#include "gpio.h"
#define ONE_BUS_LED_NUM 16
#define CYLINDER_NUM 120
#define RGB_PROTOCOL 1
//0: RGB协议，1: GRB协议
#define BRIGHT_SHIFT 4 // 亮度调整位数，0~7，数值越大亮度越暗
typedef struct {
    uint16_t ledBufferA[ONE_BUS_LED_NUM][24*4];
    uint16_t ledBufferB[ONE_BUS_LED_NUM][24*4];
} ledFrame;
extern ledFrame displayMem[CYLINDER_NUM/2];


void ledSetColorOne(uint16_t buffer[ONE_BUS_LED_NUM][24*4], uint8_t ledSeq, uint8_t ioSeq, uint32_t color);
void ledBufferClear(uint16_t buffer[ONE_BUS_LED_NUM][24*4]);
void ledBufferInit(void);
#endif