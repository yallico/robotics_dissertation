/* ESPNOW Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#ifndef ESPNOW_MAIN_H
#define ESPNOW_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_now.h"
#include "lvgl.h"

/* ESPNOW can work in both station and softap mode. It is configured in menuconfig. */
#if CONFIG_ESPNOW_WIFI_MODE_STATION
#define ESPNOW_WIFI_MODE WIFI_MODE_STA
#define ESPNOW_WIFI_IF   ESP_IF_WIFI_STA
#else
#define ESPNOW_WIFI_MODE WIFI_MODE_AP
#define ESPNOW_WIFI_IF   ESP_IF_WIFI_AP
#endif

#define ESPNOW_QUEUE_SIZE           6
#define ESPNOW_COMPLETED_BIT BIT2

#define MAX_TASKS      16        /* > number of tasks in your app   */
#define SAMPLE_US 1000000UL      /* you fire the timer every 1 s    */
#define WRAP 0x100000000ULL      /* 2^32 for wrap correction        */

extern EventGroupHandle_t s_espnow_event_group;
extern TaskHandle_t s_espnow_task_handle;
extern QueueHandle_t s_example_espnow_queue;

//#define IS_BROADCAST_ADDR(addr) (memcmp(addr, s_example_broadcast_mac, ESP_NOW_ETH_ALEN) == 0)

esp_err_t espnow_init(void);
void espnow_task(void *pvParameter);
void espnow_push_best_solution(float current_best_fitness, const float *best_solution,
    size_t gene_count, uint32_t log_id, time_t created_datetime);
void drain_buffered_messages(void);
bool validate_mac_addresses_count(void);
void espnow_deinit_all(void);

extern lv_obj_t *espnow_label;

typedef struct {
    char message[256];  // Size for the debug message
} espnow_lvgl_message_t;

typedef enum {
    EXAMPLE_ESPNOW_SEND_CB,
    EXAMPLE_ESPNOW_RECV_CB,
    EXAMPLE_ESPNOW_STOP = 99,
} example_espnow_event_id_t;

typedef struct {
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    esp_now_send_status_t status;
    uint32_t start_time_ms;
    uint32_t latency_ms;
} example_espnow_event_send_cb_t;

typedef struct {
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    uint8_t *data;
    int data_len;
} example_espnow_event_recv_cb_t;

typedef union {
    example_espnow_event_send_cb_t send_cb;
    example_espnow_event_recv_cb_t recv_cb;
} example_espnow_event_info_t;

/* When ESPNOW sending or receiving callback function is called, post event to ESPNOW task. */
typedef struct {
    example_espnow_event_id_t id;
    example_espnow_event_info_t info;
} example_espnow_event_t;

enum {
    EXAMPLE_ESPNOW_DATA_BROADCAST,
    EXAMPLE_ESPNOW_DATA_UNICAST,
    EXAMPLE_ESPNOW_DATA_MAX,
};

/* User defined field of ESPNOW data in this example. */
typedef struct {
    uint8_t type;                         //Broadcast or unicast ESPNOW data.
    uint8_t state;                        //Indicate that if has received broadcast ESPNOW data or not.
    uint16_t seq_num;                     //Sequence number of ESPNOW data.
    uint16_t crc;                         //CRC16 value of ESPNOW data.
    uint32_t magic;                       //Magic number which is used to determine which device to send unicast ESPNOW data.
    uint8_t payload[0];                   //Real payload of ESPNOW data.
} __attribute__((packed)) example_espnow_data_t;

typedef struct {
    uint8_t dest_addr[ESP_NOW_ETH_ALEN];  //MAC address of destination device.
    int len;                              //Length of ESPNOW data to be sent, unit: byte. 
    uint8_t *buffer;                      //Buffer pointing to ESPNOW data.
    uint32_t start_time_ms;               //Start time in milliseconds for sending the data.
} example_espnow_send_param_t;



#ifdef __cplusplus
}
#endif
#endif
