#include "globals.h"
#include "gui_manager.h"
#include "m5core2_axp192.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "lvgl_helpers.h"
#include "lvgl.h"
#include "rtc_m5.h"

#define LV_TICK_PERIOD_MS 1
static const char *TAG = "GUI_MANAGER";

lv_obj_t *espnow_label = NULL;
lv_obj_t *sensor_label = NULL;
lv_obj_t *t_time_label = NULL;
static lv_obj_t *battery_label = NULL;

const esp_app_desc_t *app_desc = NULL;

static void gui_timer_tick(void *arg) {
    // Unused
	(void) arg;
    lv_tick_inc(LV_TICK_PERIOD_MS);
}

void gui_manager_init(void) {

    app_desc = esp_app_get_description();

    static lv_color_t bufs[2][DISP_BUF_SIZE];
    static lv_disp_buf_t disp_buf;
    uint32_t size_in_px = DISP_BUF_SIZE;

    // Set up the frame buffers
    lv_disp_buf_init(&disp_buf, bufs[0], bufs[1], size_in_px);

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

    ESP_LOGI(TAG, "GUI Manager initialized");
}

void gui_task(void *pvParameter) {
    (void) pvParameter;

    lv_obj_t *scr = lv_disp_get_scr_act(NULL); // Get the current screen
    lv_coord_t screen_width = lv_obj_get_width(scr);
    espnow_label = lv_label_create(scr, NULL);
    sensor_label = lv_label_create(scr, NULL);
    lv_obj_t *ver_label = lv_label_create(scr, NULL);
    lv_obj_t *time_label = lv_label_create(scr, NULL);
    t_time_label = lv_label_create(scr, NULL);
    battery_label = lv_label_create(scr, NULL);

    lv_label_set_text(espnow_label, "Swarmcom Online!");
    lv_label_set_text(sensor_label, "");
    lv_label_set_text(ver_label, app_desc->version);
    lv_label_set_text(time_label, "00:00:00");
    lv_label_set_text(t_time_label, "");
    lv_label_set_text(battery_label, "Batt: --%");

    lv_obj_align(espnow_label, NULL, LV_ALIGN_CENTER, -screen_width / 4 + 10, 0);
    lv_obj_align(sensor_label, NULL, LV_ALIGN_CENTER, -screen_width / 2, 20);
    lv_obj_align(ver_label, NULL, LV_ALIGN_IN_BOTTOM_LEFT, 10, -10);
    lv_obj_align(time_label, NULL, LV_ALIGN_IN_TOP_LEFT, 10, 10);
    lv_obj_align(t_time_label, time_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);
    lv_obj_align(battery_label, NULL, LV_ALIGN_IN_TOP_RIGHT, -10, 10);

    uint32_t last_update_time = 0; // Variable to store the last update time in ticks

    while(1) {

        uint32_t current_ticks = xTaskGetTickCount(); // Get the current tick count
        // if (!experiment_started && (current_ticks >= experiment_start_ticks)) {
        // experiment_started = true;
        // } 

        // Update the time label 1 second
        if ((current_ticks - last_update_time) >= pdMS_TO_TICKS(1000)) {
            RTC_TimeTypeDef currentTime;
            RTC_GetTime(&currentTime);

            char time_str[12]; // Buffer to hold time string in HH:MM:SS format
            snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", currentTime.Hours, currentTime.Minutes, currentTime.Seconds);
            lv_label_set_text(time_label, time_str); // Update the time label with the current time

            // Update T-time label
            int t_seconds;
            char t_time_str[10];
            if (!experiment_started && experiment_start_ticks > 0) {
                t_seconds = (experiment_start_ticks - current_ticks) / portTICK_PERIOD_MS / 1000;
                snprintf(t_time_str, sizeof(t_time_str), "T-%02d", abs(t_seconds));
            } 
            else if (!experiment_started && experiment_start_ticks == 0) {
                //DO NOTHING
            }
            else {
                t_seconds = (current_ticks - experiment_start_ticks) / portTICK_PERIOD_MS / 1000;
                snprintf(t_time_str, sizeof(t_time_str), "T+%02d", t_seconds);
            }
            lv_label_set_text(t_time_label, t_time_str);

            //update the battery percentage
            float battery_pct;
            if (m5core2_axp_battery_percentage(&battery_pct) == ESP_OK) {
                char battery_str[10];
                snprintf(battery_str, sizeof(battery_str), "Batt: %.0f%%", battery_pct);
                lv_label_set_text(battery_label, battery_str);
            }

            last_update_time = current_ticks; // Update the last update time
        }

        vTaskDelay(pdMS_TO_TICKS(10)); // Delay for a short period
        lv_task_handler(); // Handle LVGL tasks
    }
}
