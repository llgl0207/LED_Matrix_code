/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "dma.h"
#include "spi.h"
#include "tim.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "tim.h"
#include "led_matrix.h"
#include <math.h>
  extern DMA_HandleTypeDef hdma_tim3_up;
  extern DMA_HandleTypeDef hdma_tim8_up;
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
uint8_t currentFrame = 0;       // 当前显示的帧索引 (0-119)
uint8_t refreshFlag = 0;        // 刷新标志，中断触发时设置为1
uint8_t isRefreshing = 0;       // 正在刷新中标志
uint32_t lastIrqTime = 0;       // 上次中断触发时间戳（用于20ms屏蔽）
#define DEBOUNCE_TIME 20         // 触发后屏蔽时间（毫秒）
uint8_t animationFrame = 0;     // 动画帧计数器，用于逐帧显示
uint32_t lastTriggerTime = 0;   // 上次有效中断触发时间（用于计算间隔）
uint32_t irqInterval = 1000;    // 两次中断的时间间隔（毫秒），初始1秒
#define TIM2_CLOCK 1000000       // TIM2经过PSC后的时钟频率（1MHz）
#define FRAME_COUNT (RAW_BUFFER_CYLINDER_NUM * 2)  // 动画总帧数（64帧完成一圈）

// 用于计算平均值的历史数据
#define SAMPLE_COUNT 10          // 取前50次的平均值
uint32_t intervalHistory[50]; // 存储最近50次中断间隔
uint8_t historyIndex = 0;        // 当前存储位置索引
uint8_t sampleCount = 0;         // 当前已有样本数量

typedef enum {
  PATTERN_TEXT = 0,
  PATTERN_CUBE = 1,
  PATTERN_SPHERE = 2,
  PATTERN_DOUBLE_HELIX = 3,
  PATTERN_SATURN = 4,
  PATTERN_RUNNER = 5
} PatternType;

static PatternType g_pattern = PATTERN_TEXT;
static const float kHeightToWidth = 2.0f;
static const PatternType kPatternCycle[] = {PATTERN_TEXT, PATTERN_CUBE, PATTERN_SPHERE, PATTERN_DOUBLE_HELIX, PATTERN_SATURN, PATTERN_RUNNER};
static uint8_t g_patternIndex = 0;
static uint32_t g_lastPatternTick = 0;
static uint32_t g_lastFrameTick = 0;
static float g_cubeAngle = 0.0f;
static const float kCubeAngleStep = 0.1745329f;
uint8_t g_brightShift = 6;

/*
注意，当前的显示器设计是RAW_BUFFER_CYLINDER_NUM个RAW切片，每个切片内包含两个数组，分别在各自对面，
这个体积显示器是对称的两个半柱面，所以每个RAW切片的数据会被转换成两个DMA切片，分别输出到GPIOD和GPIOE。
所有RAW切片遍历完一次后，显示器其实只转过了半圈，这时就要调换RAW切片下的AB输出方向，将D和E对调，重新输出。
此外，GPIO下的16个IO对应的是从上到下的16行，每一行里的16个灯对应的是从外到内的16列，所以数组末尾的灯是最内侧的灯。
特别提示，当前每一个像素都是等距分布，也就是说，两个柱面会把图像拉伸。例如要显示正方形，每个半柱面的高是宽的两倍
*/
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
/* USER CODE BEGIN PFP */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static uint32_t ledRainbowColor(float phase){
  float r = sinf(phase) * 0.5f + 0.5f;
  float g = sinf(phase + 2.0943951f) * 0.5f + 0.5f;
  float b = sinf(phase + 4.1887902f) * 0.5f + 0.5f;

  uint8_t red = (uint8_t)(r * 255.0f);
  uint8_t green = (uint8_t)(g * 255.0f);
  uint8_t blue = (uint8_t)(b * 255.0f);

  return ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue;
}

static void ledBuildPatternFrames(PatternType pattern){
  memFrameRaw *fillRaw = ledGetFillRaw();

  for (int frameIdx = 0; frameIdx < RAW_BUFFER_CYLINDER_NUM; frameIdx++) {
    ledBufferClearRaw(fillRaw[frameIdx].ledBufferRawA);
    ledBufferClearRaw(fillRaw[frameIdx].ledBufferRawB);

    switch (pattern) {
      case PATTERN_TEXT: {
        // ========== 0. 静态 A2JY09（正反两面各一个） ==========
        const uint8_t font[6][7] = {
          {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, // A
          {0x0E, 0x11, 0x02, 0x04, 0x08, 0x10, 0x1F}, // 2
          {0x07, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E}, // J
          {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, // Y
          {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, // 0
          {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x11, 0x0E}  // 9
        };
        // A面居中: (64-42)/2=11, B面居中: 64+11=75
        int startA = 11, startB = RAW_BUFFER_CYLINDER_NUM + 11;
        int colA = frameIdx;
        int colB = frameIdx + RAW_BUFFER_CYLINDER_NUM;
        for (int io = 0; io < 16; io++) {
          int row = io - 4;
          if (row >= 0 && row < 7) {
            // A面
            int rel_A = colA - startA;
            if (rel_A >= 0 && rel_A < 42) {
              int char_idx = rel_A / 7;
              int x = rel_A % 7;
              if (x < 5 && (font[char_idx][row] & (1 << (4 - x)))) {
                uint32_t color = ledRainbowColor((float)char_idx * 0.8f);
                for (int depth = 0; depth < 3; depth++)
                  ledSetColorOneRaw(fillRaw[frameIdx].ledBufferRawA, depth, io, color);
              }
            }
            // B面 — 同样位置在B侧的偏移
            int rel_B = colB - startB;
            if (rel_B >= 0 && rel_B < 42) {
              int char_idx = rel_B / 7;
              int x = rel_B % 7;
              if (x < 5 && (font[char_idx][row] & (1 << (4 - x)))) {
                uint32_t color = ledRainbowColor((float)char_idx * 0.8f);
                for (int depth = 0; depth < 3; depth++)
                  ledSetColorOneRaw(fillRaw[frameIdx].ledBufferRawB, depth, io, color);
              }
            }
          }
        }
        break;
      }
      case PATTERN_CUBE: {
        // ========== 1. 旋转的渐变立方体 ==========
        const float cubeHalf = 0.75f;
        float theta = g_cubeAngle + (3.1415926f * (float)frameIdx) / (float)RAW_BUFFER_CYLINDER_NUM;
        float c = cosf(theta);
        float s = sinf(theta);
        for (int ledPos = 0; ledPos < ONE_BUS_LED_NUM; ledPos++) {
          float r = 1.0f - ((float)ledPos / 15.0f);
          float x = r * c;
          float y = r * s;
          float ax = fabsf(x);
          float ay = fabsf(y);
          for (int io = 0; io < 16; io++) {
            float z = 1.0f - (2.0f * ((float)io / 15.0f));
            float az = fabsf(z) / kHeightToWidth;
            if ((ax <= cubeHalf) && (ay <= cubeHalf) && (az <= cubeHalf)) {
              float phase = g_cubeAngle + (float)frameIdx * 0.12f + (float)ledPos * 0.35f + (float)io * 0.22f;
              uint32_t color = ledRainbowColor(phase);
              ledSetColorOneRaw(fillRaw[frameIdx].ledBufferRawA, ledPos, io, color);
              ledSetColorOneRaw(fillRaw[frameIdx].ledBufferRawB, ledPos, io, color);
            }
          }
        }
        break;
      }
      case PATTERN_SPHERE: {
        // ========== 2. 旋转的渐变球体 ==========
        const float radius = 0.70f;
        float theta = g_cubeAngle + (3.1415926f * (float)frameIdx) / (float)RAW_BUFFER_CYLINDER_NUM;
        float c = cosf(theta);
        float s = sinf(theta);
        for (int ledPos = 0; ledPos < ONE_BUS_LED_NUM; ledPos++) {
          float r = 1.0f - ((float)ledPos / 15.0f);
          float x = r * c;
          float y = r * s;
          for (int io = 0; io < 16; io++) {
            float z = 1.0f - (2.0f * ((float)io / 15.0f));
            float z_adj = z / kHeightToWidth;
            float dist = sqrtf(x*x + y*y + z_adj*z_adj);
            if (dist <= radius) {
              float phase = dist * 4.0f - g_cubeAngle * 2.0f; // 颜色呈同心圆扩散
              uint32_t color = ledRainbowColor(phase);
              ledSetColorOneRaw(fillRaw[frameIdx].ledBufferRawA, ledPos, io, color);
              ledSetColorOneRaw(fillRaw[frameIdx].ledBufferRawB, ledPos, io, color);
            }
          }
        }
        break;
      }
      case PATTERN_DOUBLE_HELIX: {
        // ========== 3. 双螺旋 (DNA结构) ==========
        float theta = (3.1415926f * (float)frameIdx) / (float)RAW_BUFFER_CYLINDER_NUM;
        for (int io = 0; io < 16; io++) {
          float z = 1.0f - (2.0f * ((float)io / 15.0f));
          // 螺旋的两个极角（高度相关并随着时间旋转）
          float angle1 = z * 3.1415926f + g_cubeAngle * 2.0f;
          float angle2 = angle1 + 3.1415926f; // 对侧螺旋
          
          for (int ledPos = 0; ledPos < ONE_BUS_LED_NUM; ledPos++) {
            float r = 1.0f - ((float)ledPos / 15.0f);
            
            // 将点所在的空间角 theta 与两组螺旋角做差
            float d1 = fabsf(sinf((theta - angle1) / 2.0f));
            float d2 = fabsf(sinf((theta - angle2) / 2.0f));
            
            // 半径在0.6~0.8附近，且角度逼近螺旋角的轨迹被点亮
            if ((r > 0.6f && r < 0.8f) && (d1 < 0.15f || d2 < 0.15f)) {
              float phase = z * 2.0f + g_cubeAngle;
              uint32_t color = ledRainbowColor(phase);
              ledSetColorOneRaw(fillRaw[frameIdx].ledBufferRawA, ledPos, io, color);
              ledSetColorOneRaw(fillRaw[frameIdx].ledBufferRawB, ledPos, io, color);
            }
          }
        }
        break;
      }
      case PATTERN_SATURN: {
        // ========== 4. 行星+光环整体倾斜旋转 ==========
        float tilt = sinf(g_cubeAngle * 0.5f) * 0.75f;
        float ct = cosf(tilt);
        float st = sinf(tilt);
        // A面角度，B面 = A面 + 180°（绕 Z 翻转）
        float thetaA = g_cubeAngle + (3.1415926f * (float)frameIdx) / (float)RAW_BUFFER_CYLINDER_NUM;
        float thetaB = thetaA + 3.1415926f;
        float cA = cosf(thetaA), sA = sinf(thetaA);
        float cB = cosf(thetaB), sB = sinf(thetaB);
        for (int ledPos = 0; ledPos < ONE_BUS_LED_NUM; ledPos++) {
          float r = 1.0f - ((float)ledPos / 15.0f);
          for (int io = 0; io < 16; io++) {
            float z = 1.0f - (2.0f * ((float)io / 15.0f));
            float z_adj = z / kHeightToWidth;
            // ===== A 面 =====
            float xA = r * cA, yA = r * sA;
            float yrA = yA * ct - z_adj * st;
            float zrA = yA * st + z_adj * ct;
            float dist_bodyA = sqrtf(xA * xA + yrA * yrA + zrA * zrA);
            if (dist_bodyA <= 0.35f) {
              float phase = atan2f(yrA, xA) * 2.0f + zrA * 2.0f + g_cubeAngle;
              uint32_t color = ledRainbowColor(phase);
              ledSetColorOneRaw(fillRaw[frameIdx].ledBufferRawA, ledPos, io, color);
            }
            float dist_ringA = sqrtf(xA * xA + yrA * yrA);
            if (dist_ringA >= 0.45f && dist_ringA <= 0.80f && fabsf(zrA) <= 0.10f) {
              float phase = dist_ringA * 5.0f + g_cubeAngle * 2.0f;
              uint32_t color = ledRainbowColor(phase);
              ledSetColorOneRaw(fillRaw[frameIdx].ledBufferRawA, ledPos, io, color);
            }
            // ===== B 面（绕 Z 轴旋转 180° → 显示背面） =====
            float xB = r * cB, yB = r * sB;
            float yrB = yB * ct - z_adj * st;
            float zrB = yB * st + z_adj * ct;
            float dist_bodyB = sqrtf(xB * xB + yrB * yrB + zrB * zrB);
            if (dist_bodyB <= 0.35f) {
              float phase = atan2f(yrB, xB) * 2.0f + zrB * 2.0f + g_cubeAngle;
              uint32_t color = ledRainbowColor(phase);
              ledSetColorOneRaw(fillRaw[frameIdx].ledBufferRawB, ledPos, io, color);
            }
            float dist_ringB = sqrtf(xB * xB + yrB * yrB);
            if (dist_ringB >= 0.45f && dist_ringB <= 0.80f && fabsf(zrB) <= 0.10f) {
              float phase = dist_ringB * 5.0f + g_cubeAngle * 2.0f;
              uint32_t color = ledRainbowColor(phase);
              ledSetColorOneRaw(fillRaw[frameIdx].ledBufferRawB, ledPos, io, color);
            }
          }
        }
        break;
      }
      case PATTERN_RUNNER: {
        // ========== 5. 开合跳 ==========
        float phase = g_cubeAngle * 1.8f;
        float spread = fabsf(sinf(phase));
        float bounce = spread * 0.04f;
        float bodyR = 0.62f, bodyZ = -0.02f;
        float hz = 0.40f + bounce, hr2 = 0.012f;
        float nz = 0.30f + bounce, hpz = -0.14f + bounce;
        float lhx = 0.06f + spread*0.18f, lhz = 0.02f + spread*0.20f;
        float rhx = -0.06f - spread*0.18f, rhz = 0.02f + spread*0.20f;
        float lfx = 0.04f + spread*0.12f, lfz = -0.42f + bounce*0.5f;
        float rfx = -0.04f - spread*0.12f, rfz = -0.42f + bounce*0.5f;
        struct { float ax,ay,az, bx,by,bz; } segs[] = {
          {0,0,nz, 0,0,hpz},
          {0.06f,0,0.26f+bounce, lhx,0,lhz}, {-0.06f,0,0.26f+bounce, rhx,0,rhz},
          {0.04f,0,-0.12f+bounce, lfx,0,lfz}, {-0.04f,0,-0.12f+bounce, rfx,0,rfz},
        };
        // 小人固定在正前方 π/2，不随 frameIdx 移动
        float figAng = 1.5707963f; // π/2
        float cx = bodyR * cosf(figAng);
        float cy = bodyR * sinf(figAng);
        float csA = cosf(figAng), snA = sinf(figAng);
        float theta = (3.1415926f * (float)frameIdx) / (float)RAW_BUFFER_CYLINDER_NUM;
        float ct = cosf(theta), stt = sinf(theta);
        for (int ledPos = 0; ledPos < ONE_BUS_LED_NUM; ledPos++) {
          float r  = 1.0f - ((float)ledPos / 15.0f);
          float px = r * ct, py = r * stt;
          for (int io = 0; io < 16; io++) {
            float pz = (1.0f - (2.0f * ((float)io / 15.0f))) / kHeightToWidth;
            float dx = px - cx, dy = py - cy;
            float lx = -dx*snA + dy*csA;
            float ly =  dx*csA + dy*snA;
            float lz = pz - bodyZ;
            int hit = 0;
            if (lx*lx+ly*ly+(lz-hz)*(lz-hz) <= hr2) hit = 1;
            if (!hit) {
              for (int i = 0; i < 5; i++) {
                float da = segs[i].bx-segs[i].ax, db = segs[i].by-segs[i].ay, dc = segs[i].bz-segs[i].az;
                float len2 = da*da+db*db+dc*dc;
                float t = ((lx-segs[i].ax)*da+(ly-segs[i].ay)*db+(lz-segs[i].az)*dc)/(len2+0.0001f);
                if (t<0.0f)t=0.0f; if(t>1.0f)t=1.0f;
                float scx=segs[i].ax+t*da, scy=segs[i].ay+t*db, scz=segs[i].az+t*dc;
                if ((lx-scx)*(lx-scx)+(ly-scy)*(ly-scy)+(lz-scz)*(lz-scz)<=0.008f) { hit=1; break; }
              }
            }
            if (hit) {
              ledSetColorOneRaw(fillRaw[frameIdx].ledBufferRawA, ledPos, io,
                ledRainbowColor(g_cubeAngle + (float)io * 0.3f));
            }
          }
        }
        break;
      }
      default:
        break;
    }
  }

  ledSwapRawBuffers();

  memFrameRaw *renderRaw = ledGetRenderRaw();
  for (int i = 0; i < DMA_BUFFER_CYLINDER_NUM; i++) {
    ledBufferClearDma(FrameDmaA[i].ledBufferDmaA);
    ledBufferClearDma(FrameDmaA[i].ledBufferDmaB);
    ledBufferRawToDma(&FrameDmaA[i], &renderRaw[i]);

    ledBufferClearDma(FrameDmaB[i].ledBufferDmaA);
    ledBufferClearDma(FrameDmaB[i].ledBufferDmaB);
    ledBufferRawToDma(&FrameDmaB[i], &renderRaw[i + DMA_BUFFER_CYLINDER_NUM]);
  }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_TIM3_Init();
  MX_TIM2_Init();
  MX_TIM8_Init();
  MX_SPI1_Init();
  /* USER CODE BEGIN 2 */

  uint16_t g_led_frame[20]={0x0000,0xFFFF,0x0000,0xFFFF,0x0000,0xFFFF,0x0000,0xFFFF,0x0000,0xFFFF,0x0000,0xFFFF,0x0000,0xFFFF,0x0000,0xFFFF,0x0000,0xFFFF,0x0000};

  // 配置DMA回调函数（可选，但建议）
  hdma_tim3_up.XferErrorCallback = Error_Handler;
  hdma_tim8_up.XferErrorCallback = Error_Handler;
  
  // 启动定时器（必须）
  HAL_TIM_Base_Start(&htim3);
  HAL_TIM_Base_Start(&htim8);
  HAL_TIM_Base_Start_IT(&htim2);
  
  // 使能定时器DMA请求（必须）
  __HAL_TIM_ENABLE_DMA(&htim3, TIM_DMA_UPDATE);
  __HAL_TIM_ENABLE_DMA(&htim8, TIM_DMA_UPDATE);
  
  // 初始化LED缓冲区
  ledBufferInit();
  
  g_patternIndex = 0;
  g_pattern = kPatternCycle[g_patternIndex];
  ledBuildPatternFrames(g_pattern);
  g_lastFrameTick = HAL_GetTick();
  g_lastPatternTick = g_lastFrameTick;

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    uint32_t now = HAL_GetTick();
    if ((now - g_lastFrameTick) >= 50) {
      g_cubeAngle += kCubeAngleStep; // 用于动画时间变量
      if (g_cubeAngle > 6.2831852f) {
        g_cubeAngle -= 6.2831852f;
      }
      
      uint32_t switchInterval;
      if (g_pattern == PATTERN_TEXT) {
          switchInterval = 8000;
      } else if (g_pattern == PATTERN_SATURN) {
          switchInterval = 12000;
      } else if (g_pattern == PATTERN_RUNNER) {
          switchInterval = 10000;
      } else {
          switchInterval = 5000;
      }
      
      // 自动切换图案
      if ((now - g_lastPatternTick) >= switchInterval) {
        g_patternIndex = (g_patternIndex + 1) % 6; // 现在共有 6 个动画
        g_pattern = kPatternCycle[g_patternIndex];
        g_lastPatternTick = now;
      }
      
      // 文字需要更亮（偏移量越少亮度越高）
      if (g_pattern == PATTERN_TEXT) {
          g_brightShift = 2;  // 文字更亮以提高辨识度
      } else {
          g_brightShift = 6;  // 内部图形稍暗，避免光晕散开刺眼
      }

      ledBuildPatternFrames(g_pattern);
      g_lastFrameTick = now;
    }

    // 后台任务：动态加载Raw帧到DMA缓冲区
    if (isRefreshing) {
      // 计算需要预加载的帧索引（领先当前显示帧2*DMA_BUFFER_CYLINDER_NUM）
      int preloadFrame = (animationFrame + 2 * DMA_BUFFER_CYLINDER_NUM) % (RAW_BUFFER_CYLINDER_NUM * 2);
      // 转换为Raw帧索引（0-RAW_BUFFER_CYLINDER_NUM-1）
      int rawFrameIdx = preloadFrame % RAW_BUFFER_CYLINDER_NUM;
      // 计算目标DMA缓冲区索引
      int dmaIdx = rawFrameIdx % DMA_BUFFER_CYLINDER_NUM;
      
      // 确定目标缓冲区（与当前使用的缓冲区相反）
      memFrameDma *targetDma;
      if (rawFrameIdx < DMA_BUFFER_CYLINDER_NUM) {
        targetDma = &FrameDmaA[dmaIdx];
      } else {
        targetDma = &FrameDmaB[dmaIdx];
      }
      
      memFrameRaw *currentRenderRaw = ledGetRenderRaw();
      // 从Raw缓冲区转换到DMA缓冲区
      ledBufferClearDma(targetDma->ledBufferDmaA);
      ledBufferClearDma(targetDma->ledBufferDmaB);
      ledBufferRawToDma(targetDma, &currentRenderRaw[rawFrameIdx]);
    }

    // 待机状态：可以添加低功耗处理或其他任务
    HAL_Delay(10);
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 60;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 5;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == IR_IT_Pin)
  {
    // 获取当前时间戳
    uint32_t currentTime = HAL_GetTick();
    
    // 检查是否在20ms屏蔽期内（上次触发后20ms内忽略新触发）
    // 如果是第一次触发（lastIrqTime为0），则立即处理
    if (lastIrqTime == 0 || (currentTime - lastIrqTime) >= DEBOUNCE_TIME)
    {
      // 更新上次触发时间（用于20ms屏蔽期）
      lastIrqTime = currentTime;
      
      // 计算与上一次有效触发的时间间隔
      if (lastTriggerTime != 0)
      {
        uint32_t currentInterval = currentTime - lastTriggerTime;
        
        // 将当前间隔存入历史数组（环形缓冲区）
        intervalHistory[historyIndex] = currentInterval;
        historyIndex = (historyIndex + 1) % SAMPLE_COUNT;
        
        // 更新样本数量（最多SAMPLE_COUNT个）
        if (sampleCount < SAMPLE_COUNT)
        {
          sampleCount++;
        } else
        {
          // 如果是第一次触发，则将当前间隔存入历史数组
          intervalHistory[historyIndex] = currentInterval;
          historyIndex = (historyIndex + 1) % SAMPLE_COUNT;
          
          // 初始化样本数量为1
          sampleCount = 1;
        }
        
        // 计算前N次的平均值
        uint32_t sum = 0;
        for (uint8_t i = 0; i < sampleCount; i++)
        {
          sum += intervalHistory[i];
        }
        irqInterval = sum / sampleCount;
      }
      
      // 更新上一次有效触发时间
      lastTriggerTime = currentTime;
      
      // 计算新的TIM2 ARR值，使动画在平均间隔时间内完成
      // ARR = (平均间隔时间(ms) * 1000) / FRAME_COUNT - 1
      // 确保ARR至少为1（避免除零或负数）
      uint32_t newArr = (irqInterval * 1000) / FRAME_COUNT;
      if (newArr < 1) newArr = 1;
      newArr -= 1; // TIM计数器从0开始，所以减1
      
      // 更新TIM2的ARR值
      __HAL_TIM_SET_AUTORELOAD(&htim2, newArr);
      
      // 重置动画状态，立即重新开始渲染
      animationFrame = 0;
      isRefreshing = 1;
    }
  }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM2)
  {
    // 检查是否正在刷新动画
    if (isRefreshing)
    {
      // 计算当前阶段（0=前半圈AB模式, 1=后半圈BA模式）
      uint8_t phase = (animationFrame < RAW_BUFFER_CYLINDER_NUM) ? 0 : 1;
      // 计算当前帧在阶段内的索引（0-RAW_BUFFER_CYLINDER_NUM-1循环）
      int frameIdx = animationFrame % RAW_BUFFER_CYLINDER_NUM;
      
      // 根据帧索引选择缓冲区（使用取模实现循环）
      memFrameDma *currentFrame;
      int dmaIdx = frameIdx % DMA_BUFFER_CYLINDER_NUM;
      if (frameIdx < DMA_BUFFER_CYLINDER_NUM) {
        // 使用FrameDmaA
        currentFrame = &FrameDmaA[dmaIdx];
      } else {
        // 使用FrameDmaB
        currentFrame = &FrameDmaB[dmaIdx];
      }
      
      // 输出模式：前半圈AB模式，后半圈BA模式
      uint8_t outputMode = phase;
      ledPushGPIO(currentFrame, outputMode);
      
      // 更新动画帧计数器
      animationFrame++;
      
      // 检查是否播放完毕（2*RAW_BUFFER_CYLINDER_NUM帧完成一圈）
      if (animationFrame >= RAW_BUFFER_CYLINDER_NUM * 2)
      {
        // 刷新完成
        isRefreshing = 0;
        animationFrame = 0;
      }
    }
  }
}

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */