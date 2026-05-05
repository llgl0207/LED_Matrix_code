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
ledFrame displayMem[CYLINDER_NUM];

#if RGB_PROTOCOL == 0
/**
 * @brief 设置单个LED的颜色（RGB编码，带亮度控制）
 * 
 * 将指定颜色以RGB顺序编码后写入LED缓冲区，采用单极性归零码格式。
 * 亮度通过BRIGHT_SHIFT控制，牺牲颜色解析度实现亮度调节。
 * 
 * @param buffer LED缓冲区指针，二维数组[LED序号][数据位]，每个颜色占用96位(24*4)
 * @param ledSeq LED在总线上的序号，范围0~ONE_BUS_LED_NUM-1
 * @param ioSeq 数据输出的GPIO引脚序号，对应ODR寄存器的某一位(0~15)
 * @param color RGB颜色值，格式为0xRRGGBB
 * @note 亮度调整通过右移BRIGHT_SHIFT位实现，BRIGHT_SHIFT越大亮度越低
 */
void ledSetColorOne(uint16_t buffer[ONE_BUS_LED_NUM][24*4], uint8_t ledSeq, uint8_t ioSeq, uint32_t color){
    // 提取RGB分量并应用亮度控制
    uint8_t red = ((color >> 16) & 0xFF) >> BRIGHT_SHIFT;    // 红色分量，右移调整亮度
    uint8_t green = ((color >> 8) & 0xFF) >> BRIGHT_SHIFT;   // 绿色分量，右移调整亮度
    uint8_t blue = (color & 0xFF) >> BRIGHT_SHIFT;           // 蓝色分量，右移调整亮度
    
    // 组合为RGB顺序的24位颜色数据
    uint32_t rgbColor = ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue;
    
    // 从最高位开始处理（高位先发）
    for(int i = 0; i < 24; i++){
        uint8_t bit = (rgbColor >> (23 - i)) & 1; // 从bit23到bit0依次提取
        
        if(bit == 0){
            // 单极性归零码 1000: 第1位为1，其余为0
            buffer[ledSeq][i*4] = (buffer[ledSeq][i*4] & ~(1 << ioSeq)) | (1 << ioSeq);     // bit0 = 1
            buffer[ledSeq][i*4+1] = (buffer[ledSeq][i*4+1] & ~(1 << ioSeq));                // bit1 = 0  
            buffer[ledSeq][i*4+2] = (buffer[ledSeq][i*4+2] & ~(1 << ioSeq));                // bit2 = 0
            buffer[ledSeq][i*4+3] = (buffer[ledSeq][i*4+3] & ~(1 << ioSeq));                // bit3 = 0
        } else {
            // 单极性归零码 1110: 前3位为1，最后1位为0
            buffer[ledSeq][i*4] = (buffer[ledSeq][i*4] & ~(1 << ioSeq)) | (1 << ioSeq);     // bit0 = 1
            buffer[ledSeq][i*4+1] = (buffer[ledSeq][i*4+1] & ~(1 << ioSeq)) | (1 << ioSeq); // bit1 = 1
            buffer[ledSeq][i*4+2] = (buffer[ledSeq][i*4+2] & ~(1 << ioSeq)) | (1 << ioSeq); // bit2 = 1
            buffer[ledSeq][i*4+3] = (buffer[ledSeq][i*4+3] & ~(1 << ioSeq));                // bit3 = 0
        }
    }
}
#elif RGB_PROTOCOL == 1
/**
 * @brief 设置单个LED的颜色（GRB编码，带亮度控制）
 * 
 * 将指定颜色转换为GRB顺序后写入LED缓冲区，采用单极性归零码格式。
 * 亮度通过BRIGHT_SHIFT控制，牺牲颜色解析度实现亮度调节。
 * 
 * @param buffer LED缓冲区指针，二维数组[LED序号][数据位]，每个颜色占用96位(24*4)
 * @param ledSeq LED在总线上的序号，范围0~ONE_BUS_LED_NUM-1
 * @param ioSeq 数据输出的GPIO引脚序号，对应ODR寄存器的某一位(0~15)
 * @param color RGB颜色值，格式为0xRRGGBB，函数内部转换为GRB顺序
 * @note 亮度调整通过右移BRIGHT_SHIFT位实现，BRIGHT_SHIFT越大亮度越低
 */
void ledSetColorOne(uint16_t buffer[ONE_BUS_LED_NUM][24*4], uint8_t ledSeq, uint8_t ioSeq, uint32_t color){
    // 提取RGB分量并应用亮度控制
    uint8_t red = ((color >> 16) & 0xFF) >> BRIGHT_SHIFT;    // 红色分量，右移调整亮度
    uint8_t green = ((color >> 8) & 0xFF) >> BRIGHT_SHIFT;   // 绿色分量，右移调整亮度
    uint8_t blue = (color & 0xFF) >> BRIGHT_SHIFT;           // 蓝色分量，右移调整亮度
    
    // 转换为GRB顺序：Green → Red → Blue
    uint32_t grbColor = ((uint32_t)green << 16) | ((uint32_t)red << 8) | blue;
    
    // 从最高位开始处理（高位先发）
    for(int i = 0; i < 24; i++){
        uint8_t bit = (grbColor >> (23 - i)) & 1; // 从bit23到bit0依次提取
        
        if(bit == 0){
            // 单极性归零码 1000: 第1位为1，其余为0
            buffer[ledSeq][i*4] = (buffer[ledSeq][i*4] & ~(1 << ioSeq)) | (1 << ioSeq);     // bit0 = 1
            buffer[ledSeq][i*4+1] = (buffer[ledSeq][i*4+1] & ~(1 << ioSeq));                // bit1 = 0  
            buffer[ledSeq][i*4+2] = (buffer[ledSeq][i*4+2] & ~(1 << ioSeq));                // bit2 = 0
            buffer[ledSeq][i*4+3] = (buffer[ledSeq][i*4+3] & ~(1 << ioSeq));                // bit3 = 0
        } else {
            // 单极性归零码 1110: 前3位为1，最后1位为0
            buffer[ledSeq][i*4] = (buffer[ledSeq][i*4] & ~(1 << ioSeq)) | (1 << ioSeq);     // bit0 = 1
            buffer[ledSeq][i*4+1] = (buffer[ledSeq][i*4+1] & ~(1 << ioSeq)) | (1 << ioSeq); // bit1 = 1
            buffer[ledSeq][i*4+2] = (buffer[ledSeq][i*4+2] & ~(1 << ioSeq)) | (1 << ioSeq); // bit2 = 1
            buffer[ledSeq][i*4+3] = (buffer[ledSeq][i*4+3] & ~(1 << ioSeq));                // bit3 = 0
        }
    }
}
#endif

/**
 * @brief 清空LED缓冲区
 * 
 * 将指定LED缓冲区的所有数据位初始化为默认状态：
 * - 每个颜色位的bit0设置为1（起始位）
 * - bit1~bit3设置为0（空闲状态）
 * 
 * @param buffer 要清空的LED缓冲区指针
 */
void ledBufferClear(uint16_t buffer[ONE_BUS_LED_NUM][24*4]){
    for(int i = 0; i < ONE_BUS_LED_NUM; i++){
        for(int j = 0; j < 24*4; j += 4){
            buffer[i][j] = 0xFFFF;   // bit0: 所有引脚置1（起始位）
            buffer[i][j+1] = 0x0000; // bit1: 所有引脚置0
            buffer[i][j+2] = 0x0000; // bit2: 所有引脚置0
            buffer[i][j+3] = 0x0000; // bit3: 所有引脚置0
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
    for(int i = 0; i < CYLINDER_NUM; i++){
        ledBufferClear(displayMem[i].ledBuffer); // 初始化缓冲区
    }
}

void ledPushGPIO(GPIO_TypeDef * GPIOx, ledFrame * frame){
    if(GPIOx == GPIOD){
        HAL_DMA_Start_IT(&hdma_tim3_up, (uint32_t)frame->ledBuffer, (uint32_t)&GPIOD->ODR, ONE_BUS_LED_NUM*24*4);
    }else if(GPIOx == GPIOE){
        HAL_DMA_Start_IT(&hdma_tim8_up, (uint32_t)frame->ledBuffer, (uint32_t)&GPIOE->ODR, ONE_BUS_LED_NUM*24*4);
    }
}