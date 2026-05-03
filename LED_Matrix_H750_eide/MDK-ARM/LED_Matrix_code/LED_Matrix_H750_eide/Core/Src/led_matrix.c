void ledSetColorOne(uint16_t* pixel, uint32_t color) {
    // 提取低24位颜色数据
    uint32_t data = color & 0x00FFFFFF;
    
    // 每个bit对应4bit编码：0 -> 1000, 1 -> 1110
    for (int i = 0; i < 24; i++) {
        uint8_t bit = (data >> (23 - i)) & 0x01;  // 从高位开始读取
        uint16_t encoded = (bit == 0) ? 0x800 : 0xE00;  // 1000 或 1110
        
        // 将编码写入对应位置，保留其他位不变
        *pixel = (*pixel & ~0xF00) | (encoded >> 8);  // 清除当前4bit，写入新值
        pixel++;  // 移动到下一个uint16_t
    }
}