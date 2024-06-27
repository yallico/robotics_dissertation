#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "spi_flash_mmap.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "axp192.h"
#include "i2c_manager.h"
#include "m5core2_axp192.h"
#include "env_config.h"
#include "ota.h"

//Handle OTA
TaskHandle_t ota_task_handle = NULL;
EventGroupHandle_t ota_event_group; // declare the event group

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

    printf("Welcome to Swarmcom!\n");

	/* Print chip information */
	esp_chip_info_t chip_info;
	esp_chip_info(&chip_info);
	printf("This is %s chip with %d CPU cores, WiFi%s%s, ",
			CONFIG_IDF_TARGET,
			chip_info.cores,
			(chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
			(chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

	printf("silicon revision %d\n", chip_info.revision);


    size_t free_heap_size = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Heap when starting: %u", free_heap_size);

    #ifdef I2C_NUM_0
        printf("I2C_NUM_0 is defined\n");
        printf("Value of I2C_NUM_0: %d\n", I2C_NUM_0);
    #else
        printf("I2C_NUM_0 is not defined\n");
    #endif

    ESP_LOGI(TAG, "Initializing I2C & AXP192");
    m5core2_init();

    //IMPORTANT: Turn vibration off
    // vTaskDelay(pdMS_TO_TICKS(1000));
    // axp192_ioctl(&axp, AXP192_LDO3_DISABLE);

    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    //Initialize WIFI
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    //Check Heap
    free_heap_size = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Current free heap size: %u bytes", free_heap_size);

    //OTA
    // Create the OTA event group
    ota_event_group = xEventGroupCreate();
    // Create the OTA task
    xTaskCreate(ota_task, "OTA Task", 8192, NULL, 5, &ota_task_handle);
    get_sha256_of_partitions();
    //Check Heap
    free_heap_size = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Current free heap size: %u bytes", free_heap_size);
    //HTTPS request to version
    ota_check_ver();

    if (ota_task_handle != NULL) {
        ESP_LOGI(TAG, "OTA Task created successfully");
        //TODO: Need to workout how to delete OTA task if there is not update required.
    }
    //Check Heap
    free_heap_size = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Current free heap size: %u bytes", free_heap_size);

    // Set the log level for the Swarmcom tag to INFO
    esp_log_level_set("Swarmcom", ESP_LOG_INFO);
    xTaskCreate(&HelloWorld::run, "HelloWorldTask", 2048, nullptr, 5, nullptr);
}
