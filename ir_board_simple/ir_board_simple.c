#include "esp_log.h"
#include "string.h"
#include <stdint.h>
#include "ir_board_simple.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "IR_BOARD";

void i2c_external_master_init() {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = CONFIG_I2C_MANAGER_1_SDA,
        .scl_io_num = CONFIG_I2C_MANAGER_1_SCL,
        .sda_pullup_en = I2C_MANAGER_1_PULLUPS,
        .scl_pullup_en = I2C_MANAGER_1_PULLUPS,
        .master.clk_speed = CONFIG_I2C_MANAGER_1_FREQ_HZ,
        //.clk_flags = 0,
    };
    ESP_ERROR_CHECK(i2c_param_config(DUPLEX_I2C, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(DUPLEX_I2C, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0));
}

esp_err_t i2c_master_write_ir(uint8_t *data_wr, size_t size) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DUPLEX_BOARD_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, data_wr, size, true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(DUPLEX_I2C, cmd, I2C_MANAGER_1_TIMEOUT);
    i2c_cmd_link_delete(cmd);

    return ret;
}

esp_err_t i2c_master_read_ir(uint8_t *data_rd, size_t size) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DUPLEX_BOARD_ADDR << 1) | I2C_MASTER_READ, true);

    if (size > 1)
    {
        i2c_master_read(cmd, data_rd, size - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data_rd + size - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(DUPLEX_I2C, cmd, I2C_MANAGER_1_TIMEOUT);
    i2c_cmd_link_delete(cmd);

    return ret;
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

void ir_get_sensors(i2c_sensors_t *sensors) {
    uint8_t buf[10];
    esp_err_t ret = i2c_master_read_ir(buf, 10);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read from I2C: %s", esp_err_to_name(ret));
    }
    else {
        ESP_LOGI(TAG, "I2C read successful");

        // Copy the data from the buffer to the status structure
        memcpy(sensors, buf, 10);
        //print sensors
        print_sensors(sensors);
    }

    printf("Raw Status data: ");
    for(int i = 0; i < 10; i++) {
        printf("%02X ", buf[i]);
    }
    printf("\n");

}

void ir_get_status(i2c_status_t *status) {
    uint8_t buf[17];
    esp_err_t ret = i2c_master_read_ir(buf, 17);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read from I2C: %s", esp_err_to_name(ret));
    }
    else {
        ESP_LOGI(TAG, "I2C read successful");
    }

    printf("Raw Status data: ");
    for(int i = 0; i < 17; i++) {
        printf("%02X ", buf[i]);
    }
    printf("\n");

    // Copy the data from the buffer to the status structure
    memcpy(status, buf, 17);
    //print status
    print_status(status);
}



