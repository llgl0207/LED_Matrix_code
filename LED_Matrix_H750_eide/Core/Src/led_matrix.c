/**
 * @file led_matrix.c
 * @brief LED矩阵显示驱动实现文件
 * 
 * 该文件包含LED矩阵显示的核心驱动函数，包括颜色编码、缓冲区管理等功能。
 * 支持RGB和GRB两种颜色编码协议，通过宏定义RGB_PROTOCOL选择。
 */

#include "led_matrix.h"

/**
 * @brief LED显示缓冲区数组
 * 
 * 存储所有LED灯的颜色数据，每个圆柱体占用两个缓冲区(A和B)用于双缓冲显示。
 * 数据格式：displayMem[圆柱体序号].ledBufferA/B[LED序号][数据位]
 */
static memFrameRaw FrameRawBuffers[2][RAW_BUFFER_CYLINDER_NUM];
static uint8_t g_renderRawIndex = 0;
static uint8_t g_fillRawIndex = 1;
memFrameDma FrameDmaA[DMA_BUFFER_CYLINDER_NUM];
memFrameDma FrameDmaB[DMA_BUFFER_CYLINDER_NUM];

static inline void ledWriteDmaBits(uint16_t bufferDma[ONE_BUS_LED_NUM][24*4], int ledSeq, int ioSeq, uint32_t color){
    for(int bit = 0; bit < 24; bit++){
        uint8_t bitVal = (color >> (23 - bit)) & 1;
        int base = bit * 4;
        if(bitVal == 0){
            bufferDma[ledSeq][base] = (bufferDma[ledSeq][base] & ~(1 << ioSeq)) | (1 << ioSeq);
            bufferDma[ledSeq][base+1] = (bufferDma[ledSeq][base+1] & ~(1 << ioSeq));
            bufferDma[ledSeq][base+2] = (bufferDma[ledSeq][base+2] & ~(1 << ioSeq));
            bufferDma[ledSeq][base+3] = (bufferDma[ledSeq][base+3] & ~(1 << ioSeq));
        } else {
            bufferDma[ledSeq][base] = (bufferDma[ledSeq][base] & ~(1 << ioSeq)) | (1 << ioSeq);
            bufferDma[ledSeq][base+1] = (bufferDma[ledSeq][base+1] & ~(1 << ioSeq)) | (1 << ioSeq);
            bufferDma[ledSeq][base+2] = (bufferDma[ledSeq][base+2] & ~(1 << ioSeq)) | (1 << ioSeq);
            bufferDma[ledSeq][base+3] = (bufferDma[ledSeq][base+3] & ~(1 << ioSeq));
        }
    }
}

#if RGB_PROTOCOL == 0
/**
 * @brief 设置单个LED的颜色（RGB编码，带亮度控制）- Raw格式
 * 
 * 将指定颜色以RGB顺序编码后写入LED原始缓冲区。
 * 亮度通过BRIGHT_SHIFT控制，牺牲颜色解析度实现亮度调节。
 * 支持RGB888和RGB444两种颜色深度。
 * 
 * @param bufferRaw LED原始缓冲区指针，二维数组[LED序号][数据位]
 * @param ledSeq LED在总线上的序号，范围0~ONE_BUS_LED_NUM-1
 * @param ioSeq 数据输出的GPIO引脚序号，对应ODR寄存器的某一位(0~15)
 * @param color RGB颜色值，格式为0xRRGGBB
 * @note 亮度调整通过右移BRIGHT_SHIFT位实现，BRIGHT_SHIFT越大亮度越低
 */
void ledSetColorOneRaw(uint16_t bufferRaw[ONE_BUS_LED_NUM][RAW_BUFFER_BITS], uint8_t ledSeq, uint8_t ioSeq, uint32_t color){
    #if COLOR_DEPTH == 8
        uint8_t red = (color >> 16) & 0xFF;
        uint8_t green = (color >> 8) & 0xFF;
        uint8_t blue = color & 0xFF;
        uint32_t rgbColor = ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue;
        
        for(int i = 0; i < 24; i++){
            uint8_t bit = (rgbColor >> (23 - i)) & 1;
            if(bit == 0){
                bufferRaw[ledSeq][i] &= ~(1 << ioSeq);
            } else {
                bufferRaw[ledSeq][i] |= (1 << ioSeq);
            }
        }
    #elif COLOR_DEPTH == 4
        // RGB444格式：压缩为12位存储
        // 直接取原始颜色的高4位，亮度移位在RawToDma时进行
        uint8_t r4 = ((color >> 16) & 0xFF) >> 4;
        uint8_t g4 = ((color >> 8) & 0xFF) >> 4;
        uint8_t b4 = (color & 0xFF) >> 4;
        
        uint16_t rgbColor = ((uint16_t)r4 << 8) | ((uint16_t)g4 << 4) | b4;
        
        for(int i = 0; i < 12; i++){
            uint8_t bit = (rgbColor >> (11 - i)) & 1;
            if(bit == 0){
                bufferRaw[ledSeq][i] &= ~(1 << ioSeq);
            } else {
                bufferRaw[ledSeq][i] |= (1 << ioSeq);
            }
        }
    #endif
}

/**
 * @brief 设置单个LED的颜色（RGB编码，带亮度控制）- DMA格式
 * 
 * 将指定颜色以RGB顺序编码后写入LED DMA缓冲区，采用单极性归零码格式。
 * 
 * @param bufferDma LED DMA缓冲区指针，二维数组[LED序号][数据位]，每个颜色占用96位(24*4)
 * @param ledSeq LED在总线上的序号，范围0~ONE_BUS_LED_NUM-1
 * @param ioSeq 数据输出的GPIO引脚序号，对应ODR寄存器的某一位(0~15)
 * @param color RGB颜色值，格式为0xRRGGBB
 */
void ledSetColorOneDma(uint16_t bufferDma[ONE_BUS_LED_NUM][24*4], uint8_t ledSeq, uint8_t ioSeq, uint32_t color){
    uint8_t red = (color >> 16) & 0xFF;
    uint8_t green = (color >> 8) & 0xFF;
    uint8_t blue = color & 0xFF;
    uint32_t rgbColor = ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue;

    ledWriteDmaBits(bufferDma, ledSeq, ioSeq, rgbColor);
}

/**
 * @brief 将原始缓冲区数据转换为DMA格式（RGB协议）
 * 
 * 支持RGB888和RGB444两种颜色深度，RGB444会自动扩展为RGB888输出。
 * 
 * @param frameDma 目标DMA帧缓存指针
 * @param frameRaw 源原始帧缓存指针
 */
void ledBufferRawToDma(memFrameDma * frameDma, memFrameRaw * frameRaw){
    for(int led = 0; led < ONE_BUS_LED_NUM; led++){
        #if COLOR_DEPTH == 8
            // RGB888格式：亮度移位在此进行
            for(int io = 0; io < 16; io++){
                uint32_t colorA = 0;
                uint32_t colorB = 0;
                for(int bit = 0; bit < 24; bit++){
                    if((frameRaw->ledBufferRawA[led][bit] >> io) & 1){
                        colorA |= (1u << (23 - bit));
                    }
                    if((frameRaw->ledBufferRawB[led][bit] >> io) & 1){
                        colorB |= (1u << (23 - bit));
                    }
                }

                uint8_t redA = (colorA >> 16) & 0xFF;
                uint8_t greenA = (colorA >> 8) & 0xFF;
                uint8_t blueA = colorA & 0xFF;
                uint8_t redB = (colorB >> 16) & 0xFF;
                uint8_t greenB = (colorB >> 8) & 0xFF;
                uint8_t blueB = colorB & 0xFF;

                redA = (uint8_t)(redA >> BRIGHT_SHIFT);
                greenA = (uint8_t)(greenA >> BRIGHT_SHIFT);
                blueA = (uint8_t)(blueA >> BRIGHT_SHIFT);
                redB = (uint8_t)(redB >> BRIGHT_SHIFT);
                greenB = (uint8_t)(greenB >> BRIGHT_SHIFT);
                blueB = (uint8_t)(blueB >> BRIGHT_SHIFT);

                colorA = ((uint32_t)redA << 16) | ((uint32_t)greenA << 8) | blueA;
                colorB = ((uint32_t)redB << 16) | ((uint32_t)greenB << 8) | blueB;

                ledWriteDmaBits(frameDma->ledBufferDmaA, led, io, colorA);
                ledWriteDmaBits(frameDma->ledBufferDmaB, led, io, colorB);
            }
        #elif COLOR_DEPTH == 4
            // RGB444格式：先扩展到RGB888，再进行亮度移位
            for(int io = 0; io < 16; io++){
                uint16_t colorA12 = 0;
                uint16_t colorB12 = 0;
                for(int bit = 0; bit < 12; bit++){
                    if((frameRaw->ledBufferRawA[led][bit] >> io) & 1){
                        colorA12 |= (1u << (11 - bit));
                    }
                    if((frameRaw->ledBufferRawB[led][bit] >> io) & 1){
                        colorB12 |= (1u << (11 - bit));
                    }
                }

                uint8_t r4a = (colorA12 >> 8) & 0xF;
                uint8_t g4a = (colorA12 >> 4) & 0xF;
                uint8_t b4a = colorA12 & 0xF;
                uint8_t r4b = (colorB12 >> 8) & 0xF;
                uint8_t g4b = (colorB12 >> 4) & 0xF;
                uint8_t b4b = colorB12 & 0xF;

                uint8_t r8a = (uint8_t)(r4a << 4);
                uint8_t g8a = (uint8_t)(g4a << 4);
                uint8_t b8a = (uint8_t)(b4a << 4);
                uint8_t r8b = (uint8_t)(r4b << 4);
                uint8_t g8b = (uint8_t)(g4b << 4);
                uint8_t b8b = (uint8_t)(b4b << 4);

                r8a = (uint8_t)(r8a >> BRIGHT_SHIFT);
                g8a = (uint8_t)(g8a >> BRIGHT_SHIFT);
                b8a = (uint8_t)(b8a >> BRIGHT_SHIFT);
                r8b = (uint8_t)(r8b >> BRIGHT_SHIFT);
                g8b = (uint8_t)(g8b >> BRIGHT_SHIFT);
                b8b = (uint8_t)(b8b >> BRIGHT_SHIFT);

                uint32_t colorA = ((uint32_t)r8a << 16) | ((uint32_t)g8a << 8) | b8a;
                uint32_t colorB = ((uint32_t)r8b << 16) | ((uint32_t)g8b << 8) | b8b;

                ledWriteDmaBits(frameDma->ledBufferDmaA, led, io, colorA);
                ledWriteDmaBits(frameDma->ledBufferDmaB, led, io, colorB);
            }
        #endif
    }
}

#elif RGB_PROTOCOL == 1
/**
 * @brief 设置单个LED的颜色（GRB编码，带亮度控制）- Raw格式
 * 
 * 将指定颜色转换为GRB顺序后写入LED原始缓冲区。
 * 支持RGB888和RGB444两种颜色深度。
 * 
 * @param bufferRaw LED原始缓冲区指针，二维数组[LED序号][数据位]
 * @param ledSeq LED在总线上的序号，范围0~ONE_BUS_LED_NUM-1
 * @param ioSeq 数据输出的GPIO引脚序号，对应ODR寄存器的某一位(0~15)
 * @param color RGB颜色值，格式为0xRRGGBB，函数内部转换为GRB顺序
 */
void ledSetColorOneRaw(uint16_t bufferRaw[ONE_BUS_LED_NUM][RAW_BUFFER_BITS], uint8_t ledSeq, uint8_t ioSeq, uint32_t color){
    #if COLOR_DEPTH == 8
        uint8_t red = (color >> 16) & 0xFF;
        uint8_t green = (color >> 8) & 0xFF;
        uint8_t blue = color & 0xFF;
        // RGB888格式：直接存储24位颜色
        uint32_t grbColor = ((uint32_t)green << 16) | ((uint32_t)red << 8) | blue;
        
        for(int i = 0; i < 24; i++){
            uint8_t bit = (grbColor >> (23 - i)) & 1;
            if(bit == 0){
                bufferRaw[ledSeq][i] &= ~(1 << ioSeq);
            } else {
                bufferRaw[ledSeq][i] |= (1 << ioSeq);
            }
        }
    #elif COLOR_DEPTH == 4
        // RGB444格式：压缩为12位存储（GRB顺序）
        // 直接取原始颜色的高4位，亮度移位在RawToDma时进行
        uint8_t r4 = ((color >> 16) & 0xFF) >> 4;
        uint8_t g4 = ((color >> 8) & 0xFF) >> 4;
        uint8_t b4 = (color & 0xFF) >> 4;
        
        // GRB顺序编码
        uint16_t grbColor = ((uint16_t)g4 << 8) | ((uint16_t)r4 << 4) | b4;
        
        for(int i = 0; i < 12; i++){
            uint8_t bit = (grbColor >> (11 - i)) & 1;
            if(bit == 0){
                bufferRaw[ledSeq][i] &= ~(1 << ioSeq);
            } else {
                bufferRaw[ledSeq][i] |= (1 << ioSeq);
            }
        }
    #endif
}

/**
 * @brief 设置单个LED的颜色（GRB编码，带亮度控制）- DMA格式
 * 
 * 将指定颜色转换为GRB顺序后写入LED DMA缓冲区，采用单极性归零码格式。
 * 
 * @param bufferDma LED DMA缓冲区指针，二维数组[LED序号][数据位]，每个颜色占用96位(24*4)
 * @param ledSeq LED在总线上的序号，范围0~ONE_BUS_LED_NUM-1
 * @param ioSeq 数据输出的GPIO引脚序号，对应ODR寄存器的某一位(0~15)
 * @param color RGB颜色值，格式为0xRRGGBB，函数内部转换为GRB顺序
 */
void ledSetColorOneDma(uint16_t bufferDma[ONE_BUS_LED_NUM][24*4], uint8_t ledSeq, uint8_t ioSeq, uint32_t color){
    uint8_t red = (color >> 16) & 0xFF;
    uint8_t green = (color >> 8) & 0xFF;
    uint8_t blue = color & 0xFF;
    uint32_t grbColor = ((uint32_t)green << 16) | ((uint32_t)red << 8) | blue;

    ledWriteDmaBits(bufferDma, ledSeq, ioSeq, grbColor);
}

/**
 * @brief 将原始缓冲区数据转换为DMA格式（GRB协议）
 * 
 * 支持RGB888和RGB444两种颜色深度，RGB444会自动扩展为RGB888输出。
 * 
 * @param frameDma 目标DMA帧缓存指针
 * @param frameRaw 源原始帧缓存指针
 */
void ledBufferRawToDma(memFrameDma * frameDma, memFrameRaw * frameRaw){
    for(int led = 0; led < ONE_BUS_LED_NUM; led++){
        #if COLOR_DEPTH == 8
            // GRB888格式：亮度移位在此进行
            for(int io = 0; io < 16; io++){
                uint32_t colorA = 0;
                uint32_t colorB = 0;
                for(int bit = 0; bit < 24; bit++){
                    if((frameRaw->ledBufferRawA[led][bit] >> io) & 1){
                        colorA |= (1u << (23 - bit));
                    }
                    if((frameRaw->ledBufferRawB[led][bit] >> io) & 1){
                        colorB |= (1u << (23 - bit));
                    }
                }

                uint8_t greenA = (colorA >> 16) & 0xFF;
                uint8_t redA = (colorA >> 8) & 0xFF;
                uint8_t blueA = colorA & 0xFF;
                uint8_t greenB = (colorB >> 16) & 0xFF;
                uint8_t redB = (colorB >> 8) & 0xFF;
                uint8_t blueB = colorB & 0xFF;

                greenA = (uint8_t)(greenA >> BRIGHT_SHIFT);
                redA = (uint8_t)(redA >> BRIGHT_SHIFT);
                blueA = (uint8_t)(blueA >> BRIGHT_SHIFT);
                greenB = (uint8_t)(greenB >> BRIGHT_SHIFT);
                redB = (uint8_t)(redB >> BRIGHT_SHIFT);
                blueB = (uint8_t)(blueB >> BRIGHT_SHIFT);

                colorA = ((uint32_t)greenA << 16) | ((uint32_t)redA << 8) | blueA;
                colorB = ((uint32_t)greenB << 16) | ((uint32_t)redB << 8) | blueB;

                ledWriteDmaBits(frameDma->ledBufferDmaA, led, io, colorA);
                ledWriteDmaBits(frameDma->ledBufferDmaB, led, io, colorB);
            }
        #elif COLOR_DEPTH == 4
            // GRB444格式：先扩展到GRB888，再进行亮度移位
            for(int io = 0; io < 16; io++){
                uint16_t colorA12 = 0;
                uint16_t colorB12 = 0;
                for(int bit = 0; bit < 12; bit++){
                    if((frameRaw->ledBufferRawA[led][bit] >> io) & 1){
                        colorA12 |= (1u << (11 - bit));
                    }
                    if((frameRaw->ledBufferRawB[led][bit] >> io) & 1){
                        colorB12 |= (1u << (11 - bit));
                    }
                }

                uint8_t g4a = (colorA12 >> 8) & 0xF;
                uint8_t r4a = (colorA12 >> 4) & 0xF;
                uint8_t b4a = colorA12 & 0xF;
                uint8_t g4b = (colorB12 >> 8) & 0xF;
                uint8_t r4b = (colorB12 >> 4) & 0xF;
                uint8_t b4b = colorB12 & 0xF;

                uint8_t g8a = (uint8_t)(g4a << 4);
                uint8_t r8a = (uint8_t)(r4a << 4);
                uint8_t b8a = (uint8_t)(b4a << 4);
                uint8_t g8b = (uint8_t)(g4b << 4);
                uint8_t r8b = (uint8_t)(r4b << 4);
                uint8_t b8b = (uint8_t)(b4b << 4);

                g8a = (uint8_t)(g8a >> BRIGHT_SHIFT);
                r8a = (uint8_t)(r8a >> BRIGHT_SHIFT);
                b8a = (uint8_t)(b8a >> BRIGHT_SHIFT);
                g8b = (uint8_t)(g8b >> BRIGHT_SHIFT);
                r8b = (uint8_t)(r8b >> BRIGHT_SHIFT);
                b8b = (uint8_t)(b8b >> BRIGHT_SHIFT);

                uint32_t colorA = ((uint32_t)g8a << 16) | ((uint32_t)r8a << 8) | b8a;
                uint32_t colorB = ((uint32_t)g8b << 16) | ((uint32_t)r8b << 8) | b8b;

                ledWriteDmaBits(frameDma->ledBufferDmaA, led, io, colorA);
                ledWriteDmaBits(frameDma->ledBufferDmaB, led, io, colorB);
            }
        #endif
    }
}
#endif

/**
 * @brief 清空LED原始缓冲区
 * 
 * @param bufferRaw 要清空的LED原始缓冲区指针
 */
void ledBufferClearRaw(uint16_t bufferRaw[ONE_BUS_LED_NUM][RAW_BUFFER_BITS]){
    for(int i = 0; i < ONE_BUS_LED_NUM; i++){
        for(int j = 0; j < RAW_BUFFER_BITS; j++){
            bufferRaw[i][j] = 0x0000;
        }
    }
}

/**
 * @brief 清空LED DMA缓冲区
 * 
 * 将指定LED DMA缓冲区的所有数据位初始化为默认状态：
 * - 每个颜色位的bit0设置为1（起始位）
 * - bit1~bit3设置为0（空闲状态）
 * 
 * @param bufferDma 要清空的LED DMA缓冲区指针
 */
void ledBufferClearDma(uint16_t bufferDma[ONE_BUS_LED_NUM][24*4]){
    for(int i = 0; i < ONE_BUS_LED_NUM; i++){
        for(int j = 0; j < 24*4; j += 4){
            bufferDma[i][j] = 0xFFFF;
            bufferDma[i][j+1] = 0x0000;
            bufferDma[i][j+2] = 0x0000;
            bufferDma[i][j+3] = 0x0000;
        }
    }
}

/**
 * @brief 初始化所有LED显示缓冲区
 * 
 * 对所有圆柱体的缓冲区进行初始化清空操作，确保系统启动时
 * 所有LED处于已知的关闭状态。
 */
void ledBufferInit(void){
    g_renderRawIndex = 0;
    g_fillRawIndex = 1;
    for(int buf = 0; buf < 2; buf++){
        for(int i = 0; i < RAW_BUFFER_CYLINDER_NUM; i++){
            ledBufferClearRaw(FrameRawBuffers[buf][i].ledBufferRawA);
            ledBufferClearRaw(FrameRawBuffers[buf][i].ledBufferRawB);
        }
    }
    for(int i = 0; i < DMA_BUFFER_CYLINDER_NUM; i++){
        ledBufferClearDma(FrameDmaA[i].ledBufferDmaA);
        ledBufferClearDma(FrameDmaA[i].ledBufferDmaB);
        ledBufferClearDma(FrameDmaB[i].ledBufferDmaA);
        ledBufferClearDma(FrameDmaB[i].ledBufferDmaB);
    }
}

memFrameRaw * ledGetRenderRaw(void){
    return FrameRawBuffers[g_renderRawIndex];
}

memFrameRaw * ledGetFillRaw(void){
    return FrameRawBuffers[g_fillRawIndex];
}

void ledSwapRawBuffers(void){
    uint8_t oldRender = g_renderRawIndex;
    g_renderRawIndex = g_fillRawIndex;
    g_fillRawIndex = oldRender;

    for(int i = 0; i < RAW_BUFFER_CYLINDER_NUM; i++){
        ledBufferClearRaw(FrameRawBuffers[g_fillRawIndex][i].ledBufferRawA);
        ledBufferClearRaw(FrameRawBuffers[g_fillRawIndex][i].ledBufferRawB);
    }
}

/**
 * @brief 推送GPIO的DMA输出（支持AB/BA模式切换）
 * 
 * @param frame DMA帧缓存指针
 * @param mode 输出模式：0=AB模式(GPIOD→DmaA, GPIOE→DmaB)，1=BA模式(GPIOD→DmaB, GPIOE→DmaA)
 */
void ledPushGPIO(memFrameDma * frame, uint8_t mode){
    if(mode == 0){
        // AB模式：GPIOD输出DmaA，GPIOE输出DmaB
        HAL_DMA_Start_IT(&hdma_tim3_up, (uint32_t)frame->ledBufferDmaA, (uint32_t)&GPIOD->ODR, ONE_BUS_LED_NUM*24*4);
        HAL_DMA_Start_IT(&hdma_tim8_up, (uint32_t)frame->ledBufferDmaB, (uint32_t)&GPIOE->ODR, ONE_BUS_LED_NUM*24*4);
    }else{
        // BA模式：GPIOD输出DmaB，GPIOE输出DmaA
        HAL_DMA_Start_IT(&hdma_tim3_up, (uint32_t)frame->ledBufferDmaB, (uint32_t)&GPIOD->ODR, ONE_BUS_LED_NUM*24*4);
        HAL_DMA_Start_IT(&hdma_tim8_up, (uint32_t)frame->ledBufferDmaA, (uint32_t)&GPIOE->ODR, ONE_BUS_LED_NUM*24*4);
    }
}

/**
 * @brief 推送双缓冲区的GPIO输出（双缓冲机制）
 * 
 * GPIOD输出DmaA，GPIOE输出DmaB，实现两个半柱面的同步输出。
 * 
 * @param frame DMA帧缓存指针
 */
void ledPushGPIOVolume(memFrameDma * frame){
    HAL_DMA_Start_IT(&hdma_tim3_up, (uint32_t)frame->ledBufferDmaA, (uint32_t)&GPIOD->ODR, ONE_BUS_LED_NUM*24*4);
    HAL_DMA_Start_IT(&hdma_tim8_up, (uint32_t)frame->ledBufferDmaB, (uint32_t)&GPIOE->ODR, ONE_BUS_LED_NUM*24*4);
}