#include "Arduino.h"
#include "Wire.h"
#include "ircomm_data.h"
#include "esp_log.h"
#include "ir_board_arduino.h"
#include "lvgl.h"
#include "gui_manager.h"

#define I2C_ADDR  8

static const char* TAG = "I2C_Arduino";
i2c_mode_t ircomm_mode;
i2c_status_t ircomm_status;
i2c_sensors_t ircomm_sensors;

void init_arduino_i2c_wire() {
    Wire1.begin(32, 33, 100000); // Initialize I2C on port 1 with SDA on GPIO 32 and SCL on GPIO 33 at 100kHz
    ESP_LOGI(TAG, "I2C initialized at Port 1");
}


// Function to handle Mode 0: Status
void i2c_get_status() {
    ircomm_mode.mode = MODE_REPORT_STATUS;
    Wire1.beginTransmission(I2C_ADDR);
    Wire1.write((byte*)&ircomm_mode, sizeof(ircomm_mode));  // Assuming MODE_REPORT_STATUS is defined appropriately

    if (Wire1.endTransmission() == 0) {  // Check if transmission was successful
        Wire1.requestFrom(I2C_ADDR, sizeof(i2c_status_t));
        Wire1.readBytes((byte*)&ircomm_status, sizeof(i2c_status_t));

        // Log received data for debugging purposes
        ESP_LOGI(TAG, "Mode: %d", ircomm_status.mode);
        for (int i = 0; i < 4; i++) {
            ESP_LOGI(TAG, "Fail Count[%d]: %d", i, ircomm_status.fail_count[i]);
            ESP_LOGI(TAG, "Pass Count[%d]: %d", i, ircomm_status.pass_count[i]);
            }
        }

    else {
        ESP_LOGE(TAG, "Error transmitting Mode 0 request");
    }
}

void print_sensors(i2c_sensors_t *sensors) {
    ESP_LOGI(TAG, "IR Board Sensors LDR: %d %d %d", sensors->ldr[0], sensors->ldr[1], sensors->ldr[2]);
    ESP_LOGI(TAG, "IR Board Sensors Prox: %d %d", sensors->prox[0], sensors->prox[1]);
}

void i2c_get_sensors(){
    ircomm_mode.mode = MODE_REPORT_SENSORS;
    Wire1.beginTransmission(I2C_ADDR);
    Wire1.write((byte*)&ircomm_mode, sizeof(ircomm_mode));  // Assuming MODE_REPORT_SENSORS is defined appropriately

    if (Wire1.endTransmission() == 0) {  // Check if transmission was successful
        Wire1.requestFrom(I2C_ADDR, sizeof(i2c_sensors_t));
        Wire1.readBytes((byte*)&ircomm_sensors, sizeof(i2c_sensors_t));

        print_sensors(&ircomm_sensors);
    }

    else {
        ESP_LOGE(TAG, "Error transmitting Mode 17 request");
        }
}

void i2c_lvgl_task(void *pvParameter) {
    i2c_mode_t ircomm_mode_sensors;
    ircomm_mode_sensors.mode = MODE_REPORT_SENSORS;

    while(1) {
        Wire1.beginTransmission(I2C_ADDR);
        Wire1.write((byte*)&ircomm_mode_sensors, sizeof(ircomm_mode_sensors));
        if (Wire1.endTransmission() == 0) {
            Wire1.requestFrom(I2C_ADDR, sizeof(i2c_sensors_t));
            Wire1.readBytes((byte*)&ircomm_sensors, sizeof(i2c_sensors_t));

            // Update the display; ensure this is done in a thread-safe manner
            char buf[100];
            sprintf(buf, "LDR: %d, %d, %d\nProx: %d, %d", ircomm_sensors.ldr[0], ircomm_sensors.ldr[1], ircomm_sensors.ldr[2], ircomm_sensors.prox[0], ircomm_sensors.prox[1]);
            lv_label_set_text(sensor_label, buf);
    
        } 
            else {
            lv_label_set_text(sensor_label, "Sensor read error!");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));  
    }
}