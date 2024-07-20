#include "esp_log.h"
#include "string.h"
#include <stdint.h>
#include "ir_board_safe.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
 #include "driver/i2c_master.h"

static const char *TAG = "IR_BOARD";

// Declare global variables for bus_handle
static i2c_master_bus_handle_t bus_handle;
static i2c_master_dev_handle_t dev_handle;

void i2c_external_master_init() {
    i2c_master_bus_config_t i2c_mst_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = DUPLEX_I2C,
        .scl_io_num = CONFIG_I2C_MANAGER_1_SCL,
        .sda_io_num = CONFIG_I2C_MANAGER_1_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = I2C_MANAGER_1_PULLUPS,
    };

    //i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config, &bus_handle));
    ESP_ERROR_CHECK(i2c_master_probe(bus_handle, DUPLEX_BOARD_ADDR, -1));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DUPLEX_BOARD_ADDR,
        .scl_speed_hz = 100000,
        //.scl_wait_us = 100,
    };

    //i2c_master_dev_handle_t dev_handle;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle));
    
}

void print_status(i2c_status_t *status) {
    ESP_LOGI(TAG, "IR Board Status Mode: %d", status->mode);
    ESP_LOGI(TAG, "IR Board Status Fail Count: %d %d %d %d", status->fail_count[0], status->fail_count[1], status->fail_count[2], status->fail_count[3]);
    ESP_LOGI(TAG, "IR Board Status Pass Count: %d %d %d %d", status->pass_count[0], status->pass_count[1], status->pass_count[2], status->pass_count[3]);
}

void print_sensors(i2c_sensors_t *sensors) {
    ESP_LOGI(TAG, "IR Board Sensors LDR: %d %d %d", sensors->ldr[0], sensors->ldr[1], sensors->ldr[2]);
    ESP_LOGI(TAG, "IR Board Sensors Prox: %d %d", sensors->prox[0], sensors->prox[1]);
}

esp_err_t i2c_master_write_read(i2c_master_dev_handle_t i2c_dev_handle, uint8_t *w_buf,uint8_t w_len, uint8_t *r_buf, uint8_t r_len) {
    esp_err_t ret;
    ret = i2c_master_transmit_receive(i2c_dev_handle, w_buf, w_len, r_buf, r_len, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error W-R to IR Board");
        return ret;
    }
    return ESP_OK;
}

void i2c_get_status(i2c_status_t *status) {
    uint8_t buf[17];
    ir_i2c_mode_t ircomm_mode;
    ircomm_mode.mode = MODE_REPORT_STATUS;

    esp_err_t ret = i2c_master_write_read(dev_handle, (uint8_t*)&ircomm_mode, 1, buf, 17);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get status from IR Board");
        printf("Raw Status data: ");
        for(int i = 0; i < 17; i++) {
            printf("%02X ", buf[i]);
        }
        printf("\n");
    } else{
        // Copy the data from the buffer to the status structure
        memcpy(status, buf, 17);
        print_status(status);
    }

}

void i2c_get_sensors(i2c_sensors_t *sensors) {
    uint8_t buf[10];
    ir_i2c_mode_t ircomm_mode;
    ircomm_mode.mode = MODE_REPORT_SENSORS;

    esp_err_t ret = i2c_master_write_read(dev_handle, (uint8_t*)&ircomm_mode, 1, buf, 10);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get sensors from IR Board");
        printf("Raw Sensor data: ");
        for(int i = 0; i < 10; i++) {
            printf("%02X ", buf[i]);
        }
        printf("\n");
    } else{
        // Copy the data from the buffer to the sensors structure
        memcpy(sensors, buf, 10);
        print_sensors(sensors);
    }
}