#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "spi_flash_mmap.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "Arduino.h"
#include "cJSON.h"
#include <stdlib.h>

#include "globals.h"
#include "data_structures.h"
#include "data_logging.h"
#include "ir_board_arduino.h"
#include "rtc_m5.h"
#include "axp192.h"
#include "i2c_manager.h"
#include "m5core2_axp192.h"
#include "lvgl.h"
#include "lvgl_helpers.h"
#include "gui_manager.h"
#include "env_config.h"
#include "ota.h"
#include "espnow_main.h"
#include "https.h"
#include "ga.h"
#include "sd_card_manager.h"

//Robot ID
char* robot_id = get_mac_id();

//SD Card
const char* mount_point = "/sdcard";

//RTC
RTC_DateTypeDef global_date;
RTC_TimeTypeDef global_time;
volatile bool experiment_started = false;
volatile bool experiment_ended = false;
uint32_t experiment_start_ticks = 0; // Tick count when experiment starts

//Handle OTA
TaskHandle_t ota_task_handle = NULL;
EventGroupHandle_t ota_event_group; // declare the event group

//GA
TaskHandle_t ga_task_handle = NULL;

//GUI
TaskHandle_t gui_task_handle = NULL;

//Data Structures
experiment_metadata_t metadata;

static const char *TAG = "main";

extern "C" {
    void app_main();
}

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

    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    size_t free_heap_size = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Heap when starting: %u", free_heap_size);
    
    ESP_LOGI(TAG, "Initializing I2C & AXP192");
    m5core2_init();
    lvgl_i2c_locking(i2c_manager_locking());

    ESP_LOGI(TAG, "Initializing Arduino");
    initArduino();

    // Initialize the RTC
    ESP_LOGI(TAG, "RTC Module Init");
    rtc_m5_init();

    // Initialize the LCD and SPI
    ESP_LOGI(TAG, "Initializing LCD");
    lv_init();
	lvgl_driver_init();
    gui_manager_init();
    xTaskCreatePinnedToCore(gui_task, "gui_task", 10240, NULL, 0, &gui_task_handle, 1); //pin to core 1

    //Initialize the SD card
    esp_err_t sd_ret = init_sd_card(mount_point);
    if (sd_ret != ESP_OK) {
        printf("Failed to initialize SD card: %s\n", esp_err_to_name(sd_ret));
        return;
    }
    clean_sd_card(mount_point); //clean data stored in SD card

    free_heap_size = esp_get_free_heap_size(); //Check Heap
    ESP_LOGI(TAG, "Current free heap size: %u bytes", free_heap_size);

    //Test write to IR Board
    init_arduino_i2c_wire();
    vTaskDelay(pdMS_TO_TICKS(100));
    i2c_get_status();
    vTaskDelay(pdMS_TO_TICKS(100));
    xTaskCreate(i2c_lvgl_task, "I2C Sensor Task", 4096, NULL, 5, NULL);

    //Initialize WIFI
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
    vTaskDelay(pdMS_TO_TICKS(500));

    free_heap_size = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Current free heap size: %u bytes", free_heap_size);

    // Wait for connection to establish before starting OTA
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Wi-Fi Connected. Proceeding with SNTP & OTA.");
        // Initialize the RTC
        ESP_LOGI(TAG, "Initializing RTC Module");
        sync_rtc_with_ntp();

        //OTA
        ESP_LOGI(TAG, "Initializing OTA Update");
        ota_event_group = xEventGroupCreate(); // Create the OTA event group
        // Create the OTA task
        xTaskCreate(ota_task, "OTA Task", 8192, NULL, 5, &ota_task_handle);
        //HTTPS request to version
        ota_check_ver();

        if (ota_task_handle != NULL) {
            ESP_LOGI(TAG, "OTA Task created successfully");
            //TODO: Need to workout how to delete OTA task if there is no update required.
        }
        free_heap_size = esp_get_free_heap_size();
        ESP_LOGI("Check", "Free heap before init_custom_logging: %u", free_heap_size);
        ESP_LOGI("Check", "Free stack before init_custom_logging: %u", uxTaskGetStackHighWaterMark(NULL));


        //Initialize Custom Logging
        //init_custom_logging();
        //ESP_LOGI(TAG, "Custom logging initialized");

        // free_heap_size = esp_get_free_heap_size();
        // ESP_LOGI("Check", "Free heap before init_custom_logging: %u", free_heap_size);
        // ESP_LOGI("Check", "Free stack before init_custom_logging: %u", uxTaskGetStackHighWaterMark(NULL));

        //Initialize Genetic Algorithm
        init_ga();

        free_heap_size = esp_get_free_heap_size();
        ESP_LOGI("Check", "Free heap before init_custom_logging: %u", free_heap_size);
        ESP_LOGI("Check", "Free stack before init_custom_logging: %u", uxTaskGetStackHighWaterMark(NULL));
        
        // Start the experiment
        RTC_GetDate(&global_date); //get the current date
        RTC_GetTime(&global_time); //get the current time
        int delay_seconds = 60 - global_time.Seconds; //delay until the start of the next minute
        experiment_start_ticks = xTaskGetTickCount() + pdMS_TO_TICKS(delay_seconds * 1000);
        vTaskDelay(pdMS_TO_TICKS(delay_seconds * 1000));
        RTC_GetTime(&global_time); //this becomes experiment start time
        experiment_started = true;
        ESP_LOGI(TAG, "Starting experiment now.");

        //TODO: OTA JSON to set up experiment paramenters
        //metadata.experiment_id = generate_experiment_id(&global_date, &global_time);
        strncpy(metadata.experiment_id, generate_experiment_id(&global_date, &global_time), sizeof(metadata.experiment_id) - 1);
        strncpy(metadata.robot_id, robot_id, sizeof(metadata.robot_id) - 1);
        metadata.num_robots = 1; 
        for (int i = 0; i < metadata.num_robots; i++) {
            metadata.robot_ids[i] = 1001 + i; //has to be added manually?
        }
        metadata.data_link = strdup("ESPNOW");
        metadata.routing = strdup("UNICAST");
        metadata.msg_limit = 1000;
        metadata.com_type = strdup("DIRECT");
        metadata.msg_size_bytes = 32;  // Example message size
        metadata.robot_speed = 5;  // Example speed
        metadata.experiment_start = convert_to_time_t(&global_date, &global_time);
        metadata.experiment_end = 0;  // End time is not set yet
        metadata.seed = seed; 

        //Check Heap
        free_heap_size = esp_get_free_heap_size();
        ESP_LOGI(TAG, "Current free heap size: %u bytes", free_heap_size);

        //Genetic Algorithm task and pin it to core 1
        xTaskCreatePinnedToCore(ga_task,"GA Task",4096,NULL,5,&ga_task_handle,1);

        //Initialize ESPNOW UNICAST
        s_espnow_event_group = xEventGroupCreate();
        espnow_init();

        // Wait for the task to signal it has completed
        xEventGroupWaitBits(s_espnow_event_group, ESPNOW_COMPLETED_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
        RTC_GetTime(&global_time);
        metadata.experiment_end = convert_to_time_t(&global_date, &global_time);
        ESP_LOGI(TAG, "Exmperiment ended, proceeding with the rest of main.");

        //Test JSON & UPLOAD
        char *json_data = serialize_metadata_to_json(&metadata);
        if (json_data == NULL) {
            ESP_LOGE("app_main", "Failed to serialize JSON");
            return;
        }
        // Print JSON to console (optional, for debugging)
        printf("Serialized JSON:\n%s\n", json_data);

        //Save data to SD card
        sd_ret = write_data(mount_point, json_data, "experiment");
        if (sd_ret != ESP_OK) {
            ESP_LOGE(TAG,"Failed to write message data to SD card: %s", esp_err_to_name(sd_ret));
        }

        //TODO: Need a task to upload all the saved data at the end of experiment
        // Define the URL for the HTTPS request
        // const char* base_url = "https://robotics-dissertation.s3.eu-north-1.amazonaws.com/%s/experiment_metadata/test.json";
        // int needed_length = snprintf(NULL, 0, base_url, robot_id) + 1; // +1 for null terminator.
        // char* url = new char[needed_length];
        // snprintf(url, needed_length, base_url, robot_id);
        // printf("Constructed URL: %s\n", url);
        // // Send the JSON data via HTTPS
        // esp_err_t result = https_put(url, json_data, strlen(json_data));
        // if (result == ESP_OK) {
        //     ESP_LOGI("app_main", "Data posted successfully");
        // } else {
        //     ESP_LOGE("app_main", "Failed to post data");
        // }

        } else if (bits & WIFI_FAIL_BIT) {
            ESP_LOGI(TAG, "Failed to connect to Wi-Fi. OTA will not start.");
        }

    //De-init SD Card
    unmount_sd_card(mount_point);

}
