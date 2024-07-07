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

#include "axp192.h"
#include "i2c_manager.h"
#include "m5core2_axp192.h"
#include "lvgl.h"
#include "lvgl_helpers.h"
#include "env_config.h"
#include "ota.h"

#define LV_TICK_PERIOD_MS 1
#define DUPLEX_I2C	(i2c_port_t)I2C_NUM_1
#define DUPLEX_BOARD_I2C_PORT (0x08)

typedef struct i2c_sensors {
  int16_t ldr[3];     // 6 bytes
  int16_t prox[2];    // 4 bytes
} i2c_sensors_t;

typedef struct i2c_status { 
  uint8_t mode;                   // 1  bytes
  uint16_t fail_count[4];          // 8 bytes
  uint16_t pass_count[4];         // 8 bytes 
} i2c_status_t;

//Handle OTA
TaskHandle_t ota_task_handle = NULL;
EventGroupHandle_t ota_event_group; // declare the event group

static const char *TAG = "main";

axp192_t axp;

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
    lv_obj_t *label = lv_label_create(scr, NULL); // Create a label on the current screen
    lv_label_set_text(label, "Hello, Swarmcom!"); // Set the label text
    lv_obj_align(label, NULL, LV_ALIGN_CENTER, 0, 0); // Center the label on the screen

    while(1) {
        vTaskDelay(pdMS_TO_TICKS(10)); // Delay for a short period
        lv_task_handler(); // Handle LVGL tasks
    }
}

// class HelloWorld {
// public:
//     static void run(void* arg) {
//         while(true) {
//             ESP_LOGI("Swarmcom", "Hello, Swarmcom!");
//             vTaskDelay(pdMS_TO_TICKS(1000)); // 1000 ms delay
//         }
//     }
// };

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

        #ifdef I2C_NUM_1
        printf("I2C_NUM_1 is defined\n");
        printf("Value of I2C_NUM_1: %d\n", I2C_NUM_1);
    #else
        printf("I2C_NUM_1 is not defined\n");
    #endif

    //vTaskDelay(pdMS_TO_TICKS(1000));
    //axp192_ioctl(&axp, AXP192_LDO3_DISABLE);

    ESP_LOGI(TAG, "Initializing I2C & AXP192");
    m5core2_init();
    lvgl_i2c_locking(i2c_manager_locking());

    ESP_LOGI(TAG, "Initializing LCD");
    lv_init();
	lvgl_driver_init();

    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Create and start GUI task for handling LVGL related activities
    xTaskCreatePinnedToCore(guiTask, "guiTask", 4096*2, NULL, 0, NULL, 1);

    //Check Heap
    free_heap_size = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Current free heap size: %u bytes", free_heap_size);

    //Init Sensor Read
    ESP_LOGI(TAG, "Initializing IR Sensor Read");
    //i2c_status_t buffer;
    i2c_sensors_t sensors;
    uint8_t sensor_mode = 17;
    //*(i2c_port_t*)
    for (int i = 0; i < 10; i++) {
        i2c_manager_write(DUPLEX_I2C, DUPLEX_BOARD_I2C_PORT, I2C_NO_REG, (uint8_t*)&sensor_mode, 1);

        i2c_manager_read(DUPLEX_I2C, DUPLEX_BOARD_I2C_PORT, I2C_NO_REG, (uint8_t*)&sensors, 10);
        ESP_LOGI(TAG, "IR Sensor Read: %u", sensors.ldr[0]);
        ESP_LOGI(TAG, "IR Sensor Read: %u", sensors.ldr[1]);
        //ESP_LOGI(TAG, "IR Sensor Read: %d", buffer.fail_count[0]);
        //ESP_LOGI(TAG, "IR Sensor Read: %d", buffer.pass_count[0]);
    }

    //Initialize WIFI
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

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
    // esp_log_level_set("Swarmcom", ESP_LOG_INFO);
    // xTaskCreate(&HelloWorld::run, "HelloWorldTask", 2048, nullptr, 5, nullptr);
}
