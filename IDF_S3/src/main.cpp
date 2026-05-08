#include <array>
#include <cinttypes>
#include <cstring>
#include <cstdio>
#include <vector>
#include <cmath>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"

static const char *TAG = "spi_master";

#define SPI_HOST_USED      SPI2_HOST
#define PIN_NUM_MISO       GPIO_NUM_13
#define PIN_NUM_MOSI       GPIO_NUM_11
#define PIN_NUM_SCLK       GPIO_NUM_12
#define PIN_NUM_CS         GPIO_NUM_10

static spi_device_handle_t spi_handle;

static constexpr uint32_t SPI_FRAME_MAGIC = 0x4C45444D;
static constexpr uint32_t SPI_CMD_PREPARE = 0x00000001;
static constexpr uint32_t SPI_CMD_STATUS = 0x00000002;

static constexpr uint32_t SPI_STATUS_READY = 0x00000001;
static constexpr uint32_t SPI_STATUS_BUSY = 0x00000002;
static constexpr uint32_t SPI_STATUS_DONE = 0x00000003;
static constexpr uint32_t SPI_STATUS_ERROR = 0x000000FF;

static constexpr size_t ONE_BUS_LED_NUM = 16;
static constexpr size_t RAW_BUFFER_BITS = 12;
static constexpr size_t RAW_BUFFER_CYLINDER_NUM = 64;
static constexpr size_t FRAME_BYTES = RAW_BUFFER_CYLINDER_NUM * 2 * ONE_BUS_LED_NUM * RAW_BUFFER_BITS * sizeof(uint16_t);
static constexpr size_t FRAME_WORDS = FRAME_BYTES / sizeof(uint32_t);
static constexpr float kHeightToWidth = 2.0f;
static constexpr size_t SPI_MAX_CHUNK_BYTES = 4092;

struct MemFrameRaw {
    uint16_t ledBufferRawA[ONE_BUS_LED_NUM][RAW_BUFFER_BITS];
    uint16_t ledBufferRawB[ONE_BUS_LED_NUM][RAW_BUFFER_BITS];
};

static_assert(sizeof(MemFrameRaw) == ONE_BUS_LED_NUM * RAW_BUFFER_BITS * 2 * sizeof(uint16_t), "MemFrameRaw size mismatch");

static void spi_master_init()
{
    spi_bus_config_t buscfg;
    std::memset(&buscfg, 0, sizeof(buscfg));
    buscfg.mosi_io_num = PIN_NUM_MOSI;
    buscfg.miso_io_num = PIN_NUM_MISO;
    buscfg.sclk_io_num = PIN_NUM_SCLK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = SPI_MAX_CHUNK_BYTES;

    spi_device_interface_config_t devcfg;
    std::memset(&devcfg, 0, sizeof(devcfg));
    devcfg.clock_speed_hz = 8000000;
    devcfg.mode = 0;
    devcfg.spics_io_num = PIN_NUM_CS;
    devcfg.queue_size = 3;
    devcfg.flags = 0;
    devcfg.duty_cycle_pos = 128;
    devcfg.cs_ena_posttrans = 2;

    ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST_USED, &buscfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(SPI_HOST_USED, &devcfg, &spi_handle));
}

static esp_err_t spi_send_frame_chunks(const uint8_t *data, size_t length)
{
    size_t remaining = length;
    const uint8_t *ptr = data;
    esp_err_t ret = spi_device_acquire_bus(spi_handle, portMAX_DELAY);
    if (ret != ESP_OK) {
        return ret;
    }

    while (remaining > 0) {
        size_t chunk = remaining > SPI_MAX_CHUNK_BYTES ? SPI_MAX_CHUNK_BYTES : remaining;
        spi_transaction_t t;
        std::memset(&t, 0, sizeof(t));
        t.length = chunk * 8;
        t.tx_buffer = ptr;
        if (remaining > chunk) {
            t.flags = SPI_TRANS_CS_KEEP_ACTIVE;
        }

        ret = spi_device_transmit(spi_handle, &t);
        if (ret != ESP_OK) {
            break;
        }
        ptr += chunk;
        remaining -= chunk;
    }

    spi_device_release_bus(spi_handle);
    return ret;
}

static uint32_t spi_send_cmd(uint32_t cmd, uint32_t length_words)
{
    std::array<uint32_t, 3> tx = {
        __builtin_bswap32(SPI_FRAME_MAGIC),
        __builtin_bswap32(cmd),
        __builtin_bswap32(length_words)
    };
    std::array<uint32_t, 3> rx = {0};
    spi_transaction_t t;
    std::memset(&t, 0, sizeof(t));
    t.length = 96;
    t.tx_buffer = tx.data();
    t.rx_buffer = rx.data();

    esp_err_t ret = spi_device_transmit(spi_handle, &t);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI cmd failed: %s", esp_err_to_name(ret));
        return SPI_STATUS_ERROR;
    }

    return __builtin_bswap32(rx[0]);
}

static bool spi_wait_status(uint32_t desired, int retries, int delay_ms)
{
    for (int i = 0; i < retries; i++) {
        uint32_t status = spi_send_cmd(SPI_CMD_STATUS, 0);
        if (status == desired) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    return false;
}

static uint32_t led_rainbow_color(float phase)
{
    float r = sinf(phase) * 0.5f + 0.5f;
    float g = sinf(phase + 2.0943951f) * 0.5f + 0.5f;
    float b = sinf(phase + 4.1887902f) * 0.5f + 0.5f;

    uint8_t red = static_cast<uint8_t>(r * 255.0f);
    uint8_t green = static_cast<uint8_t>(g * 255.0f);
    uint8_t blue = static_cast<uint8_t>(b * 255.0f);

    return (static_cast<uint32_t>(red) << 16) | (static_cast<uint32_t>(green) << 8) | blue;
}

static void set_color_raw(uint16_t bufferRaw[ONE_BUS_LED_NUM][RAW_BUFFER_BITS], int ledPos, int io, uint32_t color)
{
    uint8_t r4 = static_cast<uint8_t>((color >> 16) & 0xFF) >> 4;
    uint8_t g4 = static_cast<uint8_t>((color >> 8) & 0xFF) >> 4;
    uint8_t b4 = static_cast<uint8_t>(color & 0xFF) >> 4;
    uint16_t grbColor = static_cast<uint16_t>((g4 << 8) | (r4 << 4) | b4);

    for (int bit = 0; bit < static_cast<int>(RAW_BUFFER_BITS); bit++) {
        uint16_t mask = static_cast<uint16_t>(1u << io);
        if ((grbColor >> (RAW_BUFFER_BITS - 1 - bit)) & 1u) {
            bufferRaw[ledPos][bit] |= mask;
        } else {
            bufferRaw[ledPos][bit] &= static_cast<uint16_t>(~mask);
        }
    }
}

static void build_cube_frame(MemFrameRaw *frame, float cubeAngle, int frameIdx)
{
    std::memset(frame, 0, sizeof(MemFrameRaw));

    const float cubeHalf = 0.75f;
    float theta = cubeAngle + (3.1415926f * static_cast<float>(frameIdx)) / static_cast<float>(RAW_BUFFER_CYLINDER_NUM);
    float c = cosf(theta);
    float s = sinf(theta);

    for (int ledPos = 0; ledPos < static_cast<int>(ONE_BUS_LED_NUM); ledPos++) {
        float r = 1.0f - (static_cast<float>(ledPos) / 15.0f);
        float x = r * c;
        float y = r * s;
        float ax = fabsf(x);
        float ay = fabsf(y);
        for (int io = 0; io < 16; io++) {
            float z = 1.0f - (2.0f * (static_cast<float>(io) / 15.0f));
            float az = fabsf(z) / kHeightToWidth;
            if ((ax <= cubeHalf) && (ay <= cubeHalf) && (az <= cubeHalf)) {
                float phase = cubeAngle + (static_cast<float>(frameIdx) * 0.12f) + (static_cast<float>(ledPos) * 0.35f) + (static_cast<float>(io) * 0.22f);
                uint32_t color = led_rainbow_color(phase);
                set_color_raw(frame->ledBufferRawA, ledPos, io, color);
                set_color_raw(frame->ledBufferRawB, ledPos, io, color);
            }
        }
    }
}

extern "C" void app_main(void);

extern "C" void app_main(void)
{
    spi_master_init();

    uint32_t *frame_words = static_cast<uint32_t *>(heap_caps_malloc(FRAME_WORDS * sizeof(uint32_t), MALLOC_CAP_8BIT));
    uint32_t *tx_words = static_cast<uint32_t *>(heap_caps_malloc(FRAME_WORDS * sizeof(uint32_t), MALLOC_CAP_DMA));
    if (!frame_words || !tx_words) {
        ESP_LOGE(TAG, "Failed to allocate frame buffer");
        return;
    }

    std::memset(frame_words, 0, FRAME_WORDS * sizeof(uint32_t));
    MemFrameRaw *frames = reinterpret_cast<MemFrameRaw *>(frame_words);
    float cubeAngle = 0.0f;
    for (size_t i = 0; i < RAW_BUFFER_CYLINDER_NUM; i++) {
        build_cube_frame(&frames[i], cubeAngle, static_cast<int>(i));
    }

    spi_transaction_t data_tx;
    std::memset(&data_tx, 0, sizeof(data_tx));
    data_tx.length = FRAME_WORDS * 32;
    data_tx.tx_buffer = tx_words;
    data_tx.rx_buffer = nullptr;

    int frame_counter = 0;
    while (1) {
        ESP_LOGI(TAG, "heartbeat");
        for (size_t i = 0; i < RAW_BUFFER_CYLINDER_NUM; i++) {
            build_cube_frame(&frames[i], cubeAngle, static_cast<int>(i));
        }

        for (size_t i = 0; i < FRAME_WORDS; i++) {
            tx_words[i] = __builtin_bswap32(frame_words[i]);
        }

        uint32_t status = spi_send_cmd(SPI_CMD_PREPARE, FRAME_WORDS);
        if (status == SPI_STATUS_BUSY) {
            ESP_LOGW(TAG, "STM32 busy, skipping frame");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(2));

        esp_err_t ret = spi_send_frame_chunks(reinterpret_cast<const uint8_t *>(tx_words), FRAME_WORDS * sizeof(uint32_t));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Frame transmit failed: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        frame_counter++;
        cubeAngle += 0.1745329f;
        if (cubeAngle > 6.2831852f) {
            cubeAngle -= 6.2831852f;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
