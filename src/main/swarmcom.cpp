#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_log.h"

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
    esp_log_level_set("Swarmcom", ESP_LOG_INFO);
    xTaskCreate(&HelloWorld::run, "HelloWorldTask", 2048, nullptr, 5, nullptr);
}
