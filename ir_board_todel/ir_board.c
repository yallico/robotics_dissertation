#include "esp_log.h"
#include "string.h"
#include "ir_board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "IR_BOARD";

esp_err_t ir_board_init(void) {
    // Initialize I2C
    ir_write(0);

    return ESP_OK;
}

void ir_write(uint8_t data) {
    uint8_t buffer[1] = { data };
    i2c_manager_write(DUPLEX_I2C, DUPLEX_BOARD_ADDR, I2C_NO_REG, buffer, 1);
}

// Function to swap the byte order of a 16-bit integer
uint16_t swap_uint16(uint16_t val) {
    return (val << 8) | (val >> 8);
}

void print_status(i2c_status_t *status) {
    ESP_LOGI(TAG, "IR Board Status Mode: %d", status->mode);
    ESP_LOGI(TAG, "IR Board Status Fail Count: %d %d %d %d", status->fail_count[0], status->fail_count[1], status->fail_count[2], status->fail_count[3]);
    ESP_LOGI(TAG, "IR Board Status Pass Count: %d %d %d %d", status->pass_count[0], status->pass_count[1], status->pass_count[2], status->pass_count[3]);
}

void ir_get_status(i2c_status_t *status, uint8_t data) {
    ir_write(data);
    uint8_t buf[17];
    i2c_manager_read(DUPLEX_I2C, DUPLEX_BOARD_ADDR, I2C_NO_REG, buf, 17);
    // Copy the data from the buffer to the status structure
    memcpy(status, buf, 17);
    // Convert the byte order if necessary
    // status->mode = buf[0];
    // status->fail_count[0] = swap_uint16(*(uint16_t *)&buf[1]);
    // status->fail_count[1] = swap_uint16(*(uint16_t *)&buf[3]);
    // status->fail_count[2] = swap_uint16(*(uint16_t *)&buf[5]);
    // status->fail_count[3] = swap_uint16(*(uint16_t *)&buf[7]);
    // status->pass_count[0] = swap_uint16(*(uint16_t *)&buf[9]);
    // status->pass_count[1] = swap_uint16(*(uint16_t *)&buf[11]);
    // status->pass_count[2] = swap_uint16(*(uint16_t *)&buf[13]);
    // status->pass_count[3] = swap_uint16(*(uint16_t *)&buf[15]);

    print_status(status);
    printf("Raw Status data: ");
    for(int i = 0; i < 17; i++) {
        printf("%02X ", buf[i]);
    }
    printf("\n");
}



