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
memFrameRaw FrameRaw[RAW_BUFFER_CYLINDER_NUM];
memFrameDma FrameDmaA[DMA_BUFFER_CYLINDER_NUM];
memFrameDma FrameDmaB[DMA_BUFFER_CYLINDER_NUM];

#if RGB_PROTOCOL == 0
/**
 * @brief 设置单个LED的颜色（RGB编码，带亮度控制）- Raw格式
 * 
 * 将指定颜色以RGB顺序编码后写入LED原始缓冲区。
 * 亮度通过BRIGHT_SHIFT控制，牺牲颜色解析度实现亮度调节。
 * 
 * @param bufferRaw LED原始缓冲区指针，二维数组[LED序号][数据位]，每个颜色占用24位
 * @param ledSeq LED在总线上的序号，范围0~ONE_BUS_LED_NUM-1
 * @param ioSeq 数据输出的GPIO引脚序号，对应ODR寄存器的某一位(0~15)
 * @param color RGB颜色值，格式为0xRRGGBB
 * @note 亮度调整通过右移BRIGHT_SHIFT位实现，BRIGHT_SHIFT越大亮度越低
 */
void ledSetColorOneRaw(uint16_t bufferRaw[ONE_BUS_LED_NUM][24], uint8_t ledSeq, uint8_t ioSeq, uint32_t color){
    uint8_t red = ((color >> 16) & 0xFF) >> BRIGHT_SHIFT;
    uint8_t green = ((color >> 8) & 0xFF) >> BRIGHT_SHIFT;
    uint8_t blue = (color & 0xFF) >> BRIGHT_SHIFT;
    
    uint32_t rgbColor = ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue;
    
    for(int i = 0; i < 24; i++){
        uint8_t bit = (rgbColor >> (23 - i)) & 1;
        if(bit == 0){
            bufferRaw[ledSeq][i] &= ~(1 << ioSeq);
        } else {
            bufferRaw[ledSeq][i] |= (1 << ioSeq);
        }
    }
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
    uint8_t red = ((color >> 16) & 0xFF) >> BRIGHT_SHIFT;
    uint8_t green = ((color >> 8) & 0xFF) >> BRIGHT_SHIFT;
    uint8_t blue = (color & 0xFF) >> BRIGHT_SHIFT;
    
    uint32_t rgbColor = ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue;
    
    for(int i = 0; i < 24; i++){
        uint8_t bit = (rgbColor >> (23 - i)) & 1;
        if(bit == 0){
            bufferDma[ledSeq][i*4] = (bufferDma[ledSeq][i*4] & ~(1 << ioSeq)) | (1 << ioSeq);
            bufferDma[ledSeq][i*4+1] = (bufferDma[ledSeq][i*4+1] & ~(1 << ioSeq));
            bufferDma[ledSeq][i*4+2] = (bufferDma[ledSeq][i*4+2] & ~(1 << ioSeq));
            bufferDma[ledSeq][i*4+3] = (bufferDma[ledSeq][i*4+3] & ~(1 << ioSeq));
        } else {
            bufferDma[ledSeq][i*4] = (bufferDma[ledSeq][i*4] & ~(1 << ioSeq)) | (1 << ioSeq);
            bufferDma[ledSeq][i*4+1] = (bufferDma[ledSeq][i*4+1] & ~(1 << ioSeq)) | (1 << ioSeq);
            bufferDma[ledSeq][i*4+2] = (bufferDma[ledSeq][i*4+2] & ~(1 << ioSeq)) | (1 << ioSeq);
            bufferDma[ledSeq][i*4+3] = (bufferDma[ledSeq][i*4+3] & ~(1 << ioSeq));
        }
    }
}

/**
 * @brief 将原始缓冲区数据转换为DMA格式（RGB协议）
 * 
 * @param frameDma 目标DMA帧缓存指针
 * @param frameRaw 源原始帧缓存指针
 */
void ledBufferRawToDma(memFrameDma * frameDma, memFrameRaw * frameRaw){
    for(int led = 0; led < ONE_BUS_LED_NUM; led++){
        for(int bit = 0; bit < 24; bit++){
            uint16_t rawValA = frameRaw->ledBufferRawA[led][bit];
            uint16_t rawValB = frameRaw->ledBufferRawB[led][bit];
            
            for(int io = 0; io < 16; io++){
                uint8_t bitA = (rawValA >> io) & 1;
                uint8_t bitB = (rawValB >> io) & 1;
                
                if(bitA == 0){
                    frameDma->ledBufferDmaA[led][bit*4] = (frameDma->ledBufferDmaA[led][bit*4] & ~(1 << io)) | (1 << io);
                    frameDma->ledBufferDmaA[led][bit*4+1] = (frameDma->ledBufferDmaA[led][bit*4+1] & ~(1 << io));
                    frameDma->ledBufferDmaA[led][bit*4+2] = (frameDma->ledBufferDmaA[led][bit*4+2] & ~(1 << io));
                    frameDma->ledBufferDmaA[led][bit*4+3] = (frameDma->ledBufferDmaA[led][bit*4+3] & ~(1 << io));
                } else {
                    frameDma->ledBufferDmaA[led][bit*4] = (frameDma->ledBufferDmaA[led][bit*4] & ~(1 << io)) | (1 << io);
                    frameDma->ledBufferDmaA[led][bit*4+1] = (frameDma->ledBufferDmaA[led][bit*4+1] & ~(1 << io)) | (1 << io);
                    frameDma->ledBufferDmaA[led][bit*4+2] = (frameDma->ledBufferDmaA[led][bit*4+2] & ~(1 << io)) | (1 << io);
                    frameDma->ledBufferDmaA[led][bit*4+3] = (frameDma->ledBufferDmaA[led][bit*4+3] & ~(1 << io));
                }
                
                if(bitB == 0){
                    frameDma->ledBufferDmaB[led][bit*4] = (frameDma->ledBufferDmaB[led][bit*4] & ~(1 << io)) | (1 << io);
                    frameDma->ledBufferDmaB[led][bit*4+1] = (frameDma->ledBufferDmaB[led][bit*4+1] & ~(1 << io));
                    frameDma->ledBufferDmaB[led][bit*4+2] = (frameDma->ledBufferDmaB[led][bit*4+2] & ~(1 << io));
                    frameDma->ledBufferDmaB[led][bit*4+3] = (frameDma->ledBufferDmaB[led][bit*4+3] & ~(1 << io));
                } else {
                    frameDma->ledBufferDmaB[led][bit*4] = (frameDma->ledBufferDmaB[led][bit*4] & ~(1 << io)) | (1 << io);
                    frameDma->ledBufferDmaB[led][bit*4+1] = (frameDma->ledBufferDmaB[led][bit*4+1] & ~(1 << io)) | (1 << io);
                    frameDma->ledBufferDmaB[led][bit*4+2] = (frameDma->ledBufferDmaB[led][bit*4+2] & ~(1 << io)) | (1 << io);
                    frameDma->ledBufferDmaB[led][bit*4+3] = (frameDma->ledBufferDmaB[led][bit*4+3] & ~(1 << io));
                }
            }
        }
    }
}

#elif RGB_PROTOCOL == 1
/**
 * @brief 设置单个LED的颜色（GRB编码，带亮度控制）- Raw格式
 * 
 * 将指定颜色转换为GRB顺序后写入LED原始缓冲区。
 * 
 * @param bufferRaw LED原始缓冲区指针，二维数组[LED序号][数据位]，每个颜色占用24位
 * @param ledSeq LED在总线上的序号，范围0~ONE_BUS_LED_NUM-1
 * @param ioSeq 数据输出的GPIO引脚序号，对应ODR寄存器的某一位(0~15)
 * @param color RGB颜色值，格式为0xRRGGBB，函数内部转换为GRB顺序
 */
void ledSetColorOneRaw(uint16_t bufferRaw[ONE_BUS_LED_NUM][24], uint8_t ledSeq, uint8_t ioSeq, uint32_t color){
    uint8_t red = ((color >> 16) & 0xFF) >> BRIGHT_SHIFT;
    uint8_t green = ((color >> 8) & 0xFF) >> BRIGHT_SHIFT;
    uint8_t blue = (color & 0xFF) >> BRIGHT_SHIFT;
    
    uint32_t grbColor = ((uint32_t)green << 16) | ((uint32_t)red << 8) | blue;
    
    for(int i = 0; i < 24; i++){
        uint8_t bit = (grbColor >> (23 - i)) & 1;
        if(bit == 0){
            bufferRaw[ledSeq][i] &= ~(1 << ioSeq);
        } else {
            bufferRaw[ledSeq][i] |= (1 << ioSeq);
        }
    }
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
    uint8_t red = ((color >> 16) & 0xFF) >> BRIGHT_SHIFT;
    uint8_t green = ((color >> 8) & 0xFF) >> BRIGHT_SHIFT;
    uint8_t blue = (color & 0xFF) >> BRIGHT_SHIFT;
    
    uint32_t grbColor = ((uint32_t)green << 16) | ((uint32_t)red << 8) | blue;
    
    for(int i = 0; i < 24; i++){
        uint8_t bit = (grbColor >> (23 - i)) & 1;
        if(bit == 0){
            bufferDma[ledSeq][i*4] = (bufferDma[ledSeq][i*4] & ~(1 << ioSeq)) | (1 << ioSeq);
            bufferDma[ledSeq][i*4+1] = (bufferDma[ledSeq][i*4+1] & ~(1 << ioSeq));
            bufferDma[ledSeq][i*4+2] = (bufferDma[ledSeq][i*4+2] & ~(1 << ioSeq));
            bufferDma[ledSeq][i*4+3] = (bufferDma[ledSeq][i*4+3] & ~(1 << ioSeq));
        } else {
            bufferDma[ledSeq][i*4] = (bufferDma[ledSeq][i*4] & ~(1 << ioSeq)) | (1 << ioSeq);
            bufferDma[ledSeq][i*4+1] = (bufferDma[ledSeq][i*4+1] & ~(1 << ioSeq)) | (1 << ioSeq);
            bufferDma[ledSeq][i*4+2] = (bufferDma[ledSeq][i*4+2] & ~(1 << ioSeq)) | (1 << ioSeq);
            bufferDma[ledSeq][i*4+3] = (bufferDma[ledSeq][i*4+3] & ~(1 << ioSeq));
        }
    }
}

/**
 * @brief 将原始缓冲区数据转换为DMA格式（GRB协议）
 * 
 * @param frameDma 目标DMA帧缓存指针
 * @param frameRaw 源原始帧缓存指针
 */
void ledBufferRawToDma(memFrameDma * frameDma, memFrameRaw * frameRaw){
    for(int led = 0; led < ONE_BUS_LED_NUM; led++){
        for(int bit = 0; bit < 24; bit++){
            uint16_t rawValA = frameRaw->ledBufferRawA[led][bit];
            uint16_t rawValB = frameRaw->ledBufferRawB[led][bit];
            
            for(int io = 0; io < 16; io++){
                uint8_t bitA = (rawValA >> io) & 1;
                uint8_t bitB = (rawValB >> io) & 1;
                
                if(bitA == 0){
                    frameDma->ledBufferDmaA[led][bit*4] = (frameDma->ledBufferDmaA[led][bit*4] & ~(1 << io)) | (1 << io);
                    frameDma->ledBufferDmaA[led][bit*4+1] = (frameDma->ledBufferDmaA[led][bit*4+1] & ~(1 << io));
                    frameDma->ledBufferDmaA[led][bit*4+2] = (frameDma->ledBufferDmaA[led][bit*4+2] & ~(1 << io));
                    frameDma->ledBufferDmaA[led][bit*4+3] = (frameDma->ledBufferDmaA[led][bit*4+3] & ~(1 << io));
                } else {
                    frameDma->ledBufferDmaA[led][bit*4] = (frameDma->ledBufferDmaA[led][bit*4] & ~(1 << io)) | (1 << io);
                    frameDma->ledBufferDmaA[led][bit*4+1] = (frameDma->ledBufferDmaA[led][bit*4+1] & ~(1 << io)) | (1 << io);
                    frameDma->ledBufferDmaA[led][bit*4+2] = (frameDma->ledBufferDmaA[led][bit*4+2] & ~(1 << io)) | (1 << io);
                    frameDma->ledBufferDmaA[led][bit*4+3] = (frameDma->ledBufferDmaA[led][bit*4+3] & ~(1 << io));
                }
                
                if(bitB == 0){
                    frameDma->ledBufferDmaB[led][bit*4] = (frameDma->ledBufferDmaB[led][bit*4] & ~(1 << io)) | (1 << io);
                    frameDma->ledBufferDmaB[led][bit*4+1] = (frameDma->ledBufferDmaB[led][bit*4+1] & ~(1 << io));
                    frameDma->ledBufferDmaB[led][bit*4+2] = (frameDma->ledBufferDmaB[led][bit*4+2] & ~(1 << io));
                    frameDma->ledBufferDmaB[led][bit*4+3] = (frameDma->ledBufferDmaB[led][bit*4+3] & ~(1 << io));
                } else {
                    frameDma->ledBufferDmaB[led][bit*4] = (frameDma->ledBufferDmaB[led][bit*4] & ~(1 << io)) | (1 << io);
                    frameDma->ledBufferDmaB[led][bit*4+1] = (frameDma->ledBufferDmaB[led][bit*4+1] & ~(1 << io)) | (1 << io);
                    frameDma->ledBufferDmaB[led][bit*4+2] = (frameDma->ledBufferDmaB[led][bit*4+2] & ~(1 << io)) | (1 << io);
                    frameDma->ledBufferDmaB[led][bit*4+3] = (frameDma->ledBufferDmaB[led][bit*4+3] & ~(1 << io));
                }
            }
        }
    }
}
#endif

/**
 * @brief 清空LED原始缓冲区
 * 
 * @param bufferRaw 要清空的LED原始缓冲区指针
 */
void ledBufferClearRaw(uint16_t bufferRaw[ONE_BUS_LED_NUM][24]){
    for(int i = 0; i < ONE_BUS_LED_NUM; i++){
        for(int j = 0; j < 24; j++){
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
    for(int i = 0; i < RAW_BUFFER_CYLINDER_NUM; i++){
        ledBufferClearRaw(FrameRaw[i].ledBufferRawA);
        ledBufferClearRaw(FrameRaw[i].ledBufferRawB);
    }
    for(int i = 0; i < DMA_BUFFER_CYLINDER_NUM; i++){
        ledBufferClearDma(FrameDmaA[i].ledBufferDmaA);
        ledBufferClearDma(FrameDmaA[i].ledBufferDmaB);
        ledBufferClearDma(FrameDmaB[i].ledBufferDmaA);
        ledBufferClearDma(FrameDmaB[i].ledBufferDmaB);
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