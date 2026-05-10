#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>

// 定义SPI引脚
#define SPI_CS_PIN   10  // CS
#define SPI_MOSI_PIN 11  // MOSI  
#define SPI_SCLK_PIN 12  // SCLK
#define SPI_MISO_PIN 13  // MISO

// 创建自定义SPI对象
SPIClass mySPI(HSPI);

// WiFi AP配置
const char* ssid = "LED_Matrix_AP";
const char* password = "12345678";

// 创建Web服务器对象，默认端口80
WebServer server(80);

// 方向命令定义（16位数据）
#define CMD_UP      0x0001    // 上
#define CMD_DOWN    0x0002    // 下  
#define CMD_LEFT    0x0004    // 左
#define CMD_RIGHT   0x0008    // 右
#define CMD_STOP    0x0000    // 停止

void sendCommandViaSPI(uint16_t command) {
  // 拉低CS选择设备
  digitalWrite(SPI_CS_PIN, LOW);
  
  // 发送16位命令
  uint16_t receivedData = mySPI.transfer16(command);
  
  // 拉高CS取消选择
  digitalWrite(SPI_CS_PIN, HIGH);
  
  Serial.print("SPI发送命令: 0x");
  Serial.print(command, HEX);
  Serial.print(", 接收: 0x");
  Serial.println(receivedData, HEX);
}

// 处理根页面请求
void handleRoot() {
  String html = "<!DOCTYPE html>"
                "<html>"
                "<head>"
                "<meta charset='UTF-8'>"
                "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
                "<title>LED矩阵控制</title>"
                "<style>"
                "body { font-family: Arial, sans-serif; text-align: center; margin: 20px; }"
                ".container { max-width: 300px; margin: 0 auto; }"
                ".direction-btn {"
                "  width: 80px; height: 80px; margin: 10px; font-size: 24px;"
                "  background-color: #4CAF50; color: white; border: none; border-radius: 10px;"
                "  cursor: pointer; user-select: none;"
                "}"
                ".direction-btn:active { background-color: #45a049; }"
                ".row { display: flex; justify-content: center; }"
                ".center-btn { margin-top: 20px; }"
                "</style>"
                "</head>"
                "<body>"
                "<h2>LED矩阵方向控制</h2>"
                "<div class='container'>"
                "<div class='row'>"
                "  <button class='direction-btn' onclick=\"sendCommand('UP')\">↑</button>"
                "</div>"
                "<div class='row'>"
                "  <button class='direction-btn' onclick=\"sendCommand('LEFT')\">←</button>"
                "  <button class='direction-btn center-btn' onclick=\"sendCommand('STOP')\">停止</button>"
                "  <button class='direction-btn' onclick=\"sendCommand('RIGHT')\">→</button>"
                "</div>"
                "<div class='row'>"
                "  <button class='direction-btn' onclick=\"sendCommand('DOWN')\">↓</button>"
                "</div>"
                "</div>"
                "<script>"
                "function sendCommand(cmd) {"
                "  fetch('/command?cmd=' + cmd)"
                "    .then(response => response.text())"
                "    .then(data => console.log('命令发送: ' + cmd));"
                "}"
                "</script>"
                "</body>"
                "</html>";
  
  server.send(200, "text/html", html);
}

// 处理命令请求
void handleCommand() {
  String cmd = server.arg("cmd");
  uint16_t command = CMD_STOP;
  
  if (cmd == "UP") {
    command = CMD_UP;
  } else if (cmd == "DOWN") {
    command = CMD_DOWN;
  } else if (cmd == "LEFT") {
    command = CMD_LEFT;
  } else if (cmd == "RIGHT") {
    command = CMD_RIGHT;
  } else if (cmd == "STOP") {
    command = CMD_STOP;
  }
  
  sendCommandViaSPI(command);
  server.send(200, "text/plain", "OK");
}

void setup() {
  // 初始化USB串口
  Serial.begin(115200);
  
  // 等待串口连接
  while (!Serial) {
    delay(10);
  }
  
  Serial.println("ESP32-S3 USB CDC 测试开始!");
  Serial.println("如果看到这条消息，说明CDC配置正确!");
  
  // 初始化SPI
  mySPI.begin(SPI_SCLK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SPI_CS_PIN);
  
  // 配置CS引脚为输出并拉高（空闲状态）
  pinMode(SPI_CS_PIN, OUTPUT);
  digitalWrite(SPI_CS_PIN, HIGH);
  
  Serial.println("SPI初始化完成!");
  
  // 配置WiFi为AP模式
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  
  Serial.print("AP模式启动成功! SSID: ");
  Serial.print(ssid);
  Serial.print(", 密码: ");
  Serial.println(password);
  Serial.print("请连接到该网络，然后在浏览器中访问: http://");
  Serial.println(IP);
  
  // 配置Web服务器路由
  server.on("/", HTTP_GET, handleRoot);
  server.on("/command", HTTP_GET, handleCommand);
  
  // 启动Web服务器
  server.begin();
  Serial.println("Web服务器已启动!");
}

void loop() {
  // 处理Web服务器客户端请求
  server.handleClient();
  
  // 保持USB串口活跃（可选）
  static unsigned long lastHeartbeatTime = 0;
  if (millis() - lastHeartbeatTime > 5000) {
    lastHeartbeatTime = millis();
    Serial.println("系统运行正常...");
  }
  
  delay(1);
}