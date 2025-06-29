/* ESPNOW Broadcast Example

   This is adapted from espnow_main.c for broadcast communication.
   All messages are sent to the broadcast MAC address (FF:FF:FF:FF:FF:FF).
*/

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include <float.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "esp_random.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_crc.h"
#include "espnow_broadcast.h"
#include "globals.h"
#include "lvgl.h"
#include "gui_manager.h"
#include "data_structures.h"
#include "ga.h"

#define ESPNOW_MAXDELAY 512

static const char *TAG = "espnow_broadcast";

EventGroupHandle_t s_espnow_event_group;
TaskHandle_t s_espnow_task_handle;
QueueHandle_t s_example_espnow_queue;

static uint32_t s_last_latency = 0; // Only one latency for broadcast
static int8_t s_last_rssi = 0; // Only one RSSI for broadcast

static uint32_t s_send_bytes = 0;
static uint32_t s_recv_bytes = 0;
static esp_timer_handle_t s_throughput_timer = NULL;

// ...existing code for CPU logging, throughput, etc...

// Broadcast MAC address
static const uint8_t broadcast_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ...existing code for validate_mac_addresses_count, check_hyper_mutation, cpu_percent_to_string, throughput_timer_cb...

// ESPNOW send callback (broadcast)
static void example_espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    example_espnow_event_t evt;
    example_espnow_event_send_cb_t *send_cb = &evt.info.send_cb;

    if (mac_addr == NULL) {
        ESP_LOGE(TAG, "Send cb arg error");
        return;
    }

    send_cb->start_time_ms = 0; // Not meaningful for broadcast
    send_cb->latency_ms = 0;
    memcpy(send_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    send_cb->status = status;
    evt.id = EXAMPLE_ESPNOW_SEND_CB;

    if (xQueueSend(s_example_espnow_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Send send queue fail");
    }

    if (status == ESP_NOW_SEND_SUCCESS) {
        s_send_bytes += sizeof(out_message_t);
    }
}

// ESPNOW receive callback (broadcast)
static void example_espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    example_espnow_event_t evt;
    example_espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;
    uint8_t * mac_addr = recv_info->src_addr;
    int8_t rssi = recv_info->rx_ctrl->rssi;

    if (mac_addr == NULL || data == NULL || len <= 0) {
        ESP_LOGE(TAG, "Receive cb arg error");
        return;
    }

    evt.id = EXAMPLE_ESPNOW_RECV_CB;
    memcpy(recv_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    recv_cb->data = malloc(len);
    if (recv_cb->data == NULL) {
        ESP_LOGE(TAG, "Malloc receive data fail");
        return;
    }
    s_last_rssi = rssi;
    memcpy(recv_cb->data, data, len);
    recv_cb->data_len = len;
    s_recv_bytes += len;
    if (xQueueSend(s_example_espnow_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Send receive queue fail");
        free(recv_cb->data);
    }
}

// ...existing code for parse_out_message, example_espnow_data_parse...

// Prepare ESPNOW data to be sent (broadcast)
void espnow_broadcast_best_solution(float current_best_fitness, const float *best_solution,
    size_t gene_count, uint32_t log_id, time_t created_datetime)
{
    out_message_t out_msg;
    memset(&out_msg, 0, sizeof(out_message_t));
    out_msg.log_id = log_id;
    out_msg.created_datetime = created_datetime;
    strncpy(out_msg.robot_id, robot_id, sizeof(out_msg.robot_id) - 1);
    out_msg.robot_id[sizeof(out_msg.robot_id) - 1] = '\0';
    int offset = 0;
    offset += snprintf(out_msg.message + offset, sizeof(out_msg.message) - offset, "%.3f|", current_best_fitness);
    for (size_t i = 0; i < gene_count && offset < (int)sizeof(out_msg.message); i++) {
        offset += snprintf(out_msg.message + offset, sizeof(out_msg.message) - offset, "%.3f|", best_solution[i]);
    }
    // Send to broadcast address
    esp_err_t err = esp_now_send(broadcast_mac, (uint8_t *)&out_msg, sizeof(out_msg));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to broadcast best solution: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Broadcasting best solution");
    }
}

// ...existing code for log_incoming_buffer_message, log_local_evaluation, drain_buffered_messages, espnow_task, etc...

esp_err_t espnow_broadcast_init(void)
{
    uint8_t own_mac[ESP_NOW_ETH_ALEN];
    ESP_ERROR_CHECK(esp_wifi_get_mac(ESP_IF_WIFI_STA, own_mac));
    s_example_espnow_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(example_espnow_event_t));
    if (s_example_espnow_queue == NULL) {
        ESP_LOGE(TAG, "Create mutex fail");
        return ESP_FAIL;
    }
    ga_buffer_queue = xQueueCreate(10, sizeof(example_espnow_event_t));
    if (ga_buffer_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create ga_buffer_queue");
        return ESP_FAIL;
    }
    // ...existing code for wifi/channel setup...
    ESP_ERROR_CHECK( esp_now_init() );
    ESP_ERROR_CHECK( esp_now_register_send_cb(example_espnow_send_cb) );
    ESP_ERROR_CHECK( esp_now_register_recv_cb(example_espnow_recv_cb) );
    // Add broadcast peer
    esp_now_peer_info_t peer = {0};
    peer.channel = CONFIG_ESPNOW_CHANNEL;
    peer.ifidx = ESPNOW_WIFI_IF;
    peer.encrypt = false;
    memcpy(peer.peer_addr, broadcast_mac, ESP_NOW_ETH_ALEN);
    if (esp_now_add_peer(&peer) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add broadcast peer");
    }
    // ...existing code for timer setup...
    ESP_LOGI(TAG, "ESPNOW broadcast initialization complete.");
    return ESP_OK;
}

void espnow_broadcast_deinit_all(void)
{
    ESP_LOGI(TAG, "De-initializing ESPNOW broadcast and stopping throughput timer");
    // ...same as espnow_main.c deinit, but for broadcast...
}

// ...rest of the code as in espnow_main, but adapted for broadcast where needed...
