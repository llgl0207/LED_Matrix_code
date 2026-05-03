#ifndef __LED_MATRIX_H
#define __LED_MATRIX_H
#include "main.h"
#include "gpio.h"
#define ONE_BUS_LED_NUM 16
#define CYLINDER_NUM 32
typedef struct {
    uint16_t ledBufferA[ONE_BUS_LED_NUM][24*4];
    uint16_t ledBufferB[ONE_BUS_LED_NUM][24*4];
} ledFrame;
extern ledFrame displayMem[CYLINDER_NUM/2];


void ledSetColorOne(uint16_t* pixel, uint32_t color);
void ledBufferInit(void);
#endif