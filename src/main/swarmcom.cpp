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
#include "dirent.h"

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

//Logging
uint32_t log_counter = 0;  //auto generated id
QueueHandle_t LogQueue = NULL; // Queue for logging
QueueHandle_t LogBodyQueue = NULL; // Queue for logging
SemaphoreHandle_t logCounterMutex = xSemaphoreCreateMutex(); // log_id the mutex

//IDs
char* experiment_id;
char* robot_id = get_mac_id();

//SD Card
const char* mount_point = "/sdcard";

//RTC
RTC_DateTypeDef global_date;
RTC_TimeTypeDef global_time;
volatile bool experiment_started = false;
volatile bool experiment_ended = false;
uint32_t experiment_start_ticks = 0; // Tick count when experiment starts
time_t experiment_start;
time_t experiment_end;

//Handle OTA
TaskHandle_t ota_task_handle = NULL;
EventGroupHandle_t ota_event_group; // declare the event group

//GA
EventGroupHandle_t ga_event_group; 

//GUI
TaskHandle_t gui_task_handle = NULL;

//Data Structures
experiment_metadata_t metadata;

static const char *TAG = "main";

extern "C" {
    void app_main();
}

void app_main() {

    /*******************************************************************************

    Section Name: Initialization
    Description: 
        This section initializes variables, sets up configuration values, 
        and prepares the system for execution.
    Inputs: 
        Config is managed via the sdkconfig file.
    Outputs:
        M5 Board is initialized.

    *******************************************************************************/

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
    i2c_pololu_command("S"); //start the Pololu's brownian motion

    //Initialize WIFI
    ESP_LOGI(TAG, "Initializing WIFI STA");
    wifi_init_sta();
    vTaskDelay(pdMS_TO_TICKS(500));

    //TODO: Implement robot communication
    //Initialize ESPNOW
    ESP_LOGI(TAG, "Initializing ESPNOW");
    s_espnow_event_group = xEventGroupCreate();
    espnow_init();
    xTaskCreate(espnow_task, "espnow_task", 4096, NULL, 4, NULL);

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
        // Check for new version
        if (ota_check_ver()) {
            ESP_LOGI(TAG, "Update required, starting OTA task.");
            xTaskCreate(ota_task, "OTA Task", 8192, NULL, 5, &ota_task_handle);
            if (ota_task_handle != NULL) {
                ESP_LOGI(TAG, "OTA Task created successfully");
            }
        } else {
            ESP_LOGI(TAG, "No update required, skipping OTA task creation.");
        }

        free_heap_size = esp_get_free_heap_size();
        ESP_LOGI("Check", "Free heap before init_custom_logging: %u", free_heap_size);
        ESP_LOGI("Check", "Free stack before init_custom_logging: %u", uxTaskGetStackHighWaterMark(NULL));


        //Initialize Logging Queue
        LogQueue = xQueueCreate(10, sizeof(event_log_t));
        LogBodyQueue = xQueueCreate(10, sizeof(event_log_message_t));
        xTaskCreate(write_task, "Write Task", 4096, NULL, 1, NULL);
        ESP_LOGI(TAG, "Logging Queue Initialized");

        free_heap_size = esp_get_free_heap_size();
        ESP_LOGI("Check", "Free heap before init_custom_logging: %u", free_heap_size);
        ESP_LOGI("Check", "Free stack before init_custom_logging: %u", uxTaskGetStackHighWaterMark(NULL));

        //Initialize Genetic Algorithm
        init_ga(true);

        free_heap_size = esp_get_free_heap_size();
        ESP_LOGI("Check", "Free heap before init_custom_logging: %u", free_heap_size);
        ESP_LOGI("Check", "Free stack before init_custom_logging: %u", uxTaskGetStackHighWaterMark(NULL));

        /*******************************************************************************

        Section Name: Run Experiment
        Description: 
            This section syncronizes the start of the experiment and creates the genetic
            algorithm task.
        Inputs: 
            Wifi connection is required to get random seed.
        Outputs:
            SD logging data is saved.

        *******************************************************************************/
        
        // Start the experiment
        RTC_GetDate(&global_date); //get the current date
        RTC_GetTime(&global_time); //get the current time
        int delay_seconds = 60 - global_time.Seconds; //delay until the start of the next minute
        experiment_start_ticks = xTaskGetTickCount() + pdMS_TO_TICKS(delay_seconds * 1000);
        vTaskDelay(pdMS_TO_TICKS(delay_seconds * 1000));
        RTC_GetTime(&global_time); //this becomes experiment start time
        experiment_started = true;
        ESP_LOGI(TAG, "Starting experiment now.");
        xTaskCreate(espnow_deinit_task, "espnow_deinit_task", 1024, NULL, 3, NULL); //timer to end
        experiment_id = generate_experiment_id(&global_date, &global_time);
        experiment_start = convert_to_time_t(&global_date, &global_time);

        //Check Heap
        free_heap_size = esp_get_free_heap_size();
        ESP_LOGI(TAG, "Current free heap size: %u bytes", free_heap_size);

        //Genetic Algorithm task and pin it to core 1
        ga_event_group = xEventGroupCreate();
        xTaskCreatePinnedToCore(ga_task,"GA Task",4096,NULL,5,&ga_task_handle,1);

        /*******************************************************************************

        Section Name: Post-Experiment
        Description: 
            This section handles the data upload to S3 at end of experiment.
        Inputs: 
            Trigger to signal that experiment has finalised.
        Outputs:
            S3 data upload

        *******************************************************************************/

        // Wait for the task to signal it has completed
        xEventGroupWaitBits(s_espnow_event_group, ESPNOW_COMPLETED_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
        experiment_ended = true;
        RTC_GetTime(&global_time);
        experiment_end = convert_to_time_t(&global_date, &global_time);
        ESP_LOGI(TAG, "Experiment has finished");
        vTaskDelay(100);

        //log metadata
        sd_card_mutex = xSemaphoreCreateMutex();
        char *json_data = log_experiment_metadata(&metadata);
        if(json_data) {
            printf("Metadata JSON:\n%s\n", json_data);
            if (xSemaphoreTake(sd_card_mutex, portMAX_DELAY) == pdTRUE) {
                //Save data to SD card
                sd_ret = write_data(mount_point, json_data, "metadata");
                xSemaphoreGive(sd_card_mutex);
                if (sd_ret != ESP_OK) {
                    ESP_LOGE(TAG,"Failed to write message data to SD card: %s", esp_err_to_name(sd_ret));
                }
            }
            free(json_data); 
        }

        //Check Heap
        free_heap_size = esp_get_free_heap_size();
        ESP_LOGI(TAG, "Current free heap size: %u bytes", free_heap_size);

        print_task_list();

        if (!is_wifi_connected()) {
            ESP_LOGE(TAG, "Wi-Fi disconnected, cannot upload files.");
        }
        //TODO: Document report with data processing architecture.
        ESP_LOGI(TAG, "Running HTTPS Unit Test...");
        esp_err_t https_test = test_https_cert_connection();
        ESP_LOGI(TAG, "Unit Test Completed, result: %s", esp_err_to_name(https_test));
        if (https_test == ESP_OK) {
            ESP_LOGI(TAG, "SSL handshake and certificate validation successful.");
        } else {
            ESP_LOGE(TAG, "SSL handshake failed! check certificates.");
        }

        //TODO: Handle ESP_ERR_HTTP_EAGAIN (wifi connection dropping)
        upload_all_sd_files();

    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Wi-Fi unavailable. Proceeding offline.");

        // Initialize Logging Queue
        LogQueue = xQueueCreate(10, sizeof(event_log_t));
        LogBodyQueue = xQueueCreate(10, sizeof(event_log_message_t));
        xTaskCreate(write_task, "Write Task", 4096, NULL, 1, NULL);
    
        // Initialize Genetic Algorithm
        init_ga(false);
    
        // Run experiment as before
        RTC_GetDate(&global_date);
        RTC_GetTime(&global_time);
        int delay_seconds = 60 - global_time.Seconds;
        experiment_start_ticks = xTaskGetTickCount() + pdMS_TO_TICKS(delay_seconds * 1000);
        vTaskDelay(pdMS_TO_TICKS(delay_seconds * 1000));
        RTC_GetTime(&global_time);
        experiment_started = true;
        ESP_LOGI(TAG, "Starting experiment in offline mode.");
        xTaskCreate(espnow_deinit_task, "espnow_deinit_task", 1024, NULL, 3, NULL);
        experiment_id = generate_experiment_id(&global_date, &global_time);
        experiment_start = convert_to_time_t(&global_date, &global_time);
    
        // Genetic Algorithm task
        ga_event_group = xEventGroupCreate();
        xTaskCreatePinnedToCore(ga_task,"GA Task",4096,NULL,5,&ga_task_handle,1);
        xEventGroupWaitBits(s_espnow_event_group, ESPNOW_COMPLETED_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
        //skip upload_all_sd_files since no WiFi
        experiment_ended = true;
        RTC_GetTime(&global_time);
        experiment_end = convert_to_time_t(&global_date, &global_time);
        ESP_LOGI(TAG, "Offline experiment has finished");
    
        }
        
    //De-init SD Card
    unmount_sd_card(mount_point);

    //Halt Pololu
    i2c_pololu_command("X");
}
