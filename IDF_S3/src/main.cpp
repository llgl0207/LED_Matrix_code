#include <array>
#include <cstring>
#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "spi_master";

#define SPI_HOST_USED      SPI2_HOST
#define PIN_NUM_MISO       GPIO_NUM_13
#define PIN_NUM_MOSI       GPIO_NUM_11
#define PIN_NUM_SCLK       GPIO_NUM_12
#define PIN_NUM_CS         GPIO_NUM_10

static spi_device_handle_t spi_handle;

static void spi_master_init()
{
    spi_bus_config_t buscfg;
    std::memset(&buscfg, 0, sizeof(buscfg));
    buscfg.mosi_io_num = PIN_NUM_MOSI;
    buscfg.miso_io_num = PIN_NUM_MISO;
    buscfg.sclk_io_num = PIN_NUM_SCLK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = 32;

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

extern "C" void app_main(void);

extern "C" void app_main(void)
{
    spi_master_init();

    std::array<uint8_t, 4> tx_data = {0xA5, 0x5A, 0x01, 0x00};
    std::array<uint8_t, 4> rx_data = {0};
    spi_transaction_t t;
    std::memset(&t, 0, sizeof(t));
    t.length = 32;
    t.tx_buffer = tx_data.data();
    t.rx_buffer = rx_data.data();

    int counter = 0;
    while (1) {
        tx_data[3] = static_cast<uint8_t>(counter);
        rx_data.fill(0);

        esp_err_t ret = spi_device_transmit(spi_handle, &t);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SPI transmit failed: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "TX=%02X %02X %02X %02X  RX=%02X %02X %02X %02X",
                     tx_data[0], tx_data[1], tx_data[2], tx_data[3],
                     rx_data[0], rx_data[1], rx_data[2], rx_data[3]);
        }

        counter++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
