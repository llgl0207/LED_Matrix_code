#include <Arduino.h>
#include <SPI.h>

// 定义 SPI 引脚
#define SPI_CS_PIN   10
#define SPI_MOSI_PIN 11
#define SPI_SCLK_PIN 12
#define SPI_MISO_PIN 13

SPIClass mySPI(HSPI);
SPISettings spiSettings(1000000, MSBFIRST, SPI_MODE0);

static const uint16_t spiTestData[16] = {
  0xA001, 0xA002, 0xA003, 0xA004,
  0xA005, 0xA006, 0xA007, 0xA008,
  0xA009, 0xA00A, 0xA00B, 0xA00C,
  0xA00D, 0xA00E, 0xA00F, 0xA010
};

void sendSpiTestFrame(const uint16_t *data, size_t count)
{
  digitalWrite(SPI_CS_PIN, LOW);
  mySPI.beginTransaction(spiSettings);

  Serial.printf("SPI 主机发送 %u 个 16-bit 数据:\r\n", count);
  for (size_t i = 0; i < count; i++)
  {
    uint16_t response = mySPI.transfer16(data[i]);
    Serial.printf("  [%02u] TX=0x%04X RX=0x%04X\r\n", i, data[i], response);
  }

  mySPI.endTransaction();
  digitalWrite(SPI_CS_PIN, HIGH);
}

void setup()
{
  Serial.begin(115200);
  // while (!Serial)
  // {
  //   delay(10);
  // }

  Serial.println("ESP32-S3 SPI 主机 DMA 测试启动");

  mySPI.begin(SPI_SCLK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SPI_CS_PIN);
  mySPI.setHwCs(false);

  pinMode(SPI_CS_PIN, OUTPUT);
  digitalWrite(SPI_CS_PIN, HIGH);

  Serial.println("SPI 初始化完成，CS 空闲高电平");
}

void loop()
{
  sendSpiTestFrame(spiTestData, sizeof(spiTestData) / sizeof(spiTestData[0]));
  delay(1000);
  
}
