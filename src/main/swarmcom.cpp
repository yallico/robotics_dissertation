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

#include "ir_board_arduino.h"
#include "rtc_m5.h"
#include "axp192.h"
#include "i2c_manager.h"
#include "m5core2_axp192.h"
#include "lvgl.h"
#include "lvgl_helpers.h"
#include "env_config.h"
#include "ota.h"
#include "espnow_main.h"
#include "https.h"
//#include "sd_card.h"
#include "sd_card_manager.h"
#include "data_structures.h"

//LVGL
#define LV_TICK_PERIOD_MS 1
lv_obj_t *espnow_label = NULL;
lv_obj_t *sensor_label = NULL;
lv_obj_t *t_time_label = NULL; // displaying T-time

//SD Card
const char* mount_point = "/sdcard";

//RTC
RTC_TimeTypeDef global_time;
volatile bool experiment_started = false;
uint32_t experiment_start_time = 0; // Tick count when experiment starts

//Handle OTA
TaskHandle_t ota_task_handle = NULL;
EventGroupHandle_t ota_event_group; // declare the event group

//Data Structures
experiment_metadata_t metadata;

static const char *TAG = "main";

extern "C" {
    void app_main();
}

static void gui_timer_tick(void *arg)
{
	// Unused
	(void) arg;

	lv_tick_inc(LV_TICK_PERIOD_MS);
}

static void guiTask(void *pvParameter) {

    (void) pvParameter;

    static lv_color_t bufs[2][DISP_BUF_SIZE];
	static lv_disp_buf_t disp_buf;
    static const esp_app_desc_t *app_desc = esp_app_get_description();
	uint32_t size_in_px = DISP_BUF_SIZE;

	// Set up the frame buffers
	lv_disp_buf_init(&disp_buf, &bufs[0], &bufs[1], size_in_px);

	// Set up the display driver
	lv_disp_drv_t disp_drv;
	lv_disp_drv_init(&disp_drv);
	disp_drv.flush_cb = disp_driver_flush;
	disp_drv.buffer = &disp_buf;
	lv_disp_drv_register(&disp_drv);

    // Timer to drive the main lvgl tick
	const esp_timer_create_args_t periodic_timer_args = {
		.callback = &gui_timer_tick,
		.name = "periodic_gui"
	};
	esp_timer_handle_t periodic_timer;
	ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
	ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, LV_TICK_PERIOD_MS * 1000));

    lv_obj_t *scr = lv_disp_get_scr_act(NULL); // Get the current screen
    lv_coord_t screen_width = lv_obj_get_width(scr); // Get the width of the screen
    espnow_label = lv_label_create(scr, NULL); // ESPNOW Label
    sensor_label = lv_label_create(scr, NULL); // Sensor Label
    lv_obj_t *ver_label = lv_label_create(scr, NULL); // Version Label
    lv_obj_t *time_label = lv_label_create(scr, NULL); // Time Label
    t_time_label = lv_label_create(scr, NULL); // T-time Label

    lv_label_set_text(espnow_label, "Swarmcom Online!");
    lv_label_set_text(sensor_label, "");
    lv_label_set_text(ver_label, app_desc->version);
    lv_label_set_text(time_label, "00:00:00"); // Initial text for the time label
    lv_label_set_text(t_time_label, "");

    lv_obj_align(espnow_label, NULL, LV_ALIGN_CENTER, -screen_width/4+10, 0); // Center the label on the screen
    lv_obj_align(sensor_label, NULL, LV_ALIGN_CENTER, -screen_width/2, 20); // Center the label on the screen
    lv_obj_align(ver_label, NULL, LV_ALIGN_IN_BOTTOM_LEFT, 10, -10); // Align the version label to the bottom left of the screen
    lv_obj_align(time_label, NULL, LV_ALIGN_IN_TOP_LEFT, 10, 10); // Align the time label to the top right of the screen
    lv_obj_align(t_time_label, time_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10); // Just below time-label

    uint32_t last_update_time = 0; // Variable to store the last update time in ticks

    while(1) {

        uint32_t current_time = xTaskGetTickCount(); // Get the current tick count
        // if (!experiment_started && (current_time >= experiment_start_time)) {
        // experiment_started = true;
        // } 

        // Update the time label 1 second
        if ((current_time - last_update_time) >= pdMS_TO_TICKS(1000)) {
            RTC_TimeTypeDef currentTime;
            RTC_GetTime(&currentTime);

            char time_str[12]; // Buffer to hold time string in HH:MM:SS format
            snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", currentTime.Hours, currentTime.Minutes, currentTime.Seconds);
            lv_label_set_text(time_label, time_str); // Update the time label with the current time

            // Update T-time label
            int t_seconds;
            char t_time_str[10];
            if (!experiment_started && experiment_start_time > 0) {
                t_seconds = (experiment_start_time - current_time) / portTICK_PERIOD_MS / 1000;
                snprintf(t_time_str, sizeof(t_time_str), "T-%02d", abs(t_seconds));
            } 
            else if (!experiment_started && experiment_start_time == 0) {
                //DO NOTHING
            }
            else {
                t_seconds = (current_time - experiment_start_time) / portTICK_PERIOD_MS / 1000;
                snprintf(t_time_str, sizeof(t_time_str), "T+%02d", t_seconds);
            }
            lv_label_set_text(t_time_label, t_time_str);


            last_update_time = current_time; // Update the last update time
        }

        vTaskDelay(pdMS_TO_TICKS(10)); // Delay for a short period
        lv_task_handler(); // Handle LVGL tasks
    }
}

char* serialize_metadata_to_json(const experiment_metadata_t *metadata) {
    cJSON *root = cJSON_CreateObject();
    
    cJSON_AddNumberToObject(root, "num_robots", metadata->num_robots);
    cJSON *ids_array = cJSON_AddArrayToObject(root, "robot_ids");
    for (int i = 0; i < metadata->num_robots; i++) {
        cJSON_AddItemToArray(ids_array, cJSON_CreateNumber(metadata->robot_ids[i]));
    }
    
    cJSON_AddStringToObject(root, "data_link", metadata->data_link);
    cJSON_AddStringToObject(root, "routing", metadata->routing);
    cJSON_AddNumberToObject(root, "msg_limit", metadata->msg_limit);
    cJSON_AddStringToObject(root, "com_type", metadata->com_type);
    cJSON_AddNumberToObject(root, "msg_size_bytes", metadata->msg_size_bytes);
    cJSON_AddNumberToObject(root, "robot_speed", metadata->robot_speed);

    char *json_data = cJSON_Print(root);
    cJSON_Delete(root);  // Free the cJSON object

    return json_data;  // Caller must free this string
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

    // Initialize the SD card
    //sd_card_init();
    //xTaskCreate(sd_card_write_task, "SD Write Task", 2048, NULL, 5, NULL);

    ESP_LOGI(TAG, "Initializing LCD");
    lv_init();
	lvgl_driver_init();
    xTaskCreatePinnedToCore(guiTask, "guiTask", 4096*2, NULL, 0, NULL, 1); // Create and start GUI task for handling LVGL tasks

    //Initialize the SD card
    esp_err_t sd_ret = init_sd_card(mount_point);
    if (sd_ret != ESP_OK) {
        printf("Failed to initialize SD card: %s\n", esp_err_to_name(sd_ret));
        return;
    }
    //clean_sd_card(mount_point); //clean data stored in SD card

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
        get_sha256_of_partitions();
        //Check Heap
        free_heap_size = esp_get_free_heap_size();
        ESP_LOGI(TAG, "Current free heap size: %u bytes", free_heap_size);
        //HTTPS request to version
        ota_check_ver();

        if (ota_task_handle != NULL) {
            ESP_LOGI(TAG, "OTA Task created successfully");
            //TODO: Need to workout how to delete OTA task if there is no update required.
        }

        //TODO: OTA JSON to set up experiment paramenters
        metadata.num_robots = 5; 
        for (int i = 0; i < metadata.num_robots; i++) {
            metadata.robot_ids[i] = 1001 + i; 
        }
        metadata.data_link = strdup("tes");
        metadata.routing = strdup("test");
        metadata.msg_limit = 1000;
        metadata.com_type = strdup("test");
        metadata.msg_size_bytes = 32;  // Example message size
        metadata.robot_speed = 5;  // Example speed
        
        // Start the experiment
        RTC_GetTime(&global_time); //get the current time
        int delay_seconds = 60 - global_time.Seconds; //delay until the start of the next minute
        experiment_start_time = xTaskGetTickCount() + pdMS_TO_TICKS(delay_seconds * 1000);
        vTaskDelay(pdMS_TO_TICKS(delay_seconds * 1000));
        experiment_started = true;
        ESP_LOGI(TAG, "Starting experiment now.");


        //Test JSON & UPLOAD
        char *json_data = serialize_metadata_to_json(&metadata);
        if (json_data == NULL) {
            ESP_LOGE("app_main", "Failed to serialize JSON");
            return;
        }
        // Print JSON to console (optional, for debugging)
        printf("Serialized JSON:\n%s\n", json_data);
        // Define the URL for the HTTPS request
        char* robot_id = get_mac_id();
        const char* base_url = "https://robotics-dissertation.s3.eu-north-1.amazonaws.com/%s/experiment_metadata/test.json";
        int needed_length = snprintf(NULL, 0, base_url, robot_id) + 1; // +1 for null terminator.
        char* url = new char[needed_length];
        snprintf(url, needed_length, base_url, robot_id);
        printf("Constructed URL: %s\n", url);
        // Send the JSON data via HTTPS
        esp_err_t result = https_put(url, json_data, strlen(json_data));
        if (result == ESP_OK) {
            ESP_LOGI("app_main", "Data posted successfully");
        } else {
            ESP_LOGE("app_main", "Failed to post data");
        }

        //Test saving data to SD card
        // sd_ret = write_data(mount_point, json_data, "experiment");
        // if (sd_ret != ESP_OK) {
        //     printf("Failed to write message data\n");
        // }


        } else if (bits & WIFI_FAIL_BIT) {
            ESP_LOGI(TAG, "Failed to connect to Wi-Fi. OTA will not start.");
        }

    //Check Heap
    free_heap_size = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Current free heap size: %u bytes", free_heap_size);

    //TODEL
    // const char *file_hello = "/sdcard/hello.txt";
    // char data[64];
    // snprintf(data, 64, "%s %s!\n", "Hello", "World");
    // sd_ret = s_example_write_file(file_hello, data);
    // if (sd_ret != ESP_OK) {
    //     ESP_LOGE(TAG, "Failed to write Hello World to SD Card.");
    // }
    // ret = s_example_read_file(file_hello);
    // if (ret != ESP_OK) {
    //     ESP_LOGE(TAG, "Failed to read Hello World to SD Card.");
    // }

    //test_sd_card();

    // s_unmount_card(mount_point);

    //Initialize ESPNOW UNICAST
    espnow_init();

}
