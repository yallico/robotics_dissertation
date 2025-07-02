#ifndef GUI_MANAGER_H
#define GUI_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include <stdbool.h>
#include "esp_app_desc.h"

// Declarations of the labels as externs
extern lv_obj_t *wifi_label; 
extern lv_obj_t *espnow_label;
extern lv_obj_t *sensor_label;
extern lv_obj_t *t_time_label;

//app version
extern const esp_app_desc_t *app_desc;

// Declaration of the initialization function
void gui_manager_init(void);
void gui_task(void *pvParameter);
void gui_update_wifi_icon(bool connected);
void gui_show_ota_icon(bool show);

#ifdef __cplusplus
}
#endif

#endif // GUI_MANAGER_H
