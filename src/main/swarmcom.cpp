#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "spi_flash_mmap.h"
#include "esp_log.h"
#include "axp192.h"
#include "i2c_helper.h" //TODO: understand what this is and how it works

static const char *TAG = "main";

axp192_t axp;

extern "C" {
    void app_main();
}

class HelloWorld {
public:
    static void run(void* arg) {
        while(true) {
            ESP_LOGI("Swarmcom", "Hello, Swarmcom!");
            vTaskDelay(pdMS_TO_TICKS(1000)); // 1000 ms delay
        }
    }
};

void app_main() {

    //ESP_LOGI(TAG, "SDK version: %s", esp_get_idf_version());
    //ESP_LOGI(TAG, "Heap when starting: %d", esp_get_free_heap_size());

    static i2c_port_t i2c_port = I2C_NUM_0;

    ESP_LOGI(TAG, "Initializing I2C");
    i2c_init(i2c_port);

    ESP_LOGI(TAG, "Initializing AXP192");
    axp.read = &i2c_read;
    axp.write = &i2c_write;
    axp.handle = &i2c_port;

    axp192_init(&axp);

    //IMPORTANT: Turn vibration off
    vTaskDelay(pdMS_TO_TICKS(1000));
    axp192_ioctl(&axp, AXP192_LDO3_DISABLE);

    // Set the log level for the Swarmcom tag to INFO
    esp_log_level_set("Swarmcom", ESP_LOG_INFO);
    xTaskCreate(&HelloWorld::run, "HelloWorldTask", 2048, nullptr, 5, nullptr);
}
