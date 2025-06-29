/* ESPNOW Broadcast Example Header
   Adapted from espnow_main.h for broadcast communication.
*/

#ifndef ESPNOW_BROADCAST_H
#define ESPNOW_BROADCAST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_now.h"
#include "lvgl.h"

#if CONFIG_ESPNOW_WIFI_MODE_STATION
#define ESPNOW_WIFI_MODE WIFI_MODE_STA
#define ESPNOW_WIFI_IF   ESP_IF_WIFI_STA
#else
#define ESPNOW_WIFI_MODE WIFI_MODE_AP
#define ESPNOW_WIFI_IF   ESP_IF_WIFI_AP
#endif

#define ESPNOW_QUEUE_SIZE           6
#define ESPNOW_COMPLETED_BIT BIT2

#define MAX_TASKS      16
#define SAMPLE_US 1000000UL
#define WRAP 0x100000000ULL

extern EventGroupHandle_t s_espnow_event_group;
extern TaskHandle_t s_espnow_task_handle;
extern QueueHandle_t s_example_espnow_queue;

esp_err_t espnow_broadcast_init(void);
//void espnow_task(void *pvParameter);
void espnow_broadcast_best_solution(float current_best_fitness, const float *best_solution,
    size_t gene_count, uint32_t log_id, time_t created_datetime);
//void drain_buffered_messages(void);
//void espnow_broadcast_deinit_all(void);

extern lv_obj_t *espnow_label;

typedef struct {
    char message[256];
} espnow_lvgl_message_t;

typedef enum {
    EXAMPLE_ESPNOW_SEND_CB,
    EXAMPLE_ESPNOW_RECV_CB,
    EXAMPLE_ESPNOW_STOP = 99,
} example_espnow_event_id_t;

#ifdef __cplusplus
}
#endif

#endif // ESPNOW_BROADCAST_H
