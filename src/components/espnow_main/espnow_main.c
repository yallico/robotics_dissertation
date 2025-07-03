/* ESPNOW Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
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
#include "espnow_main.h"
#include "globals.h"
#include "lvgl.h"
#include "gui_manager.h"
#include "data_structures.h"
#include "ga.h"

#define ESPNOW_MAXDELAY 512

static const char *TAG = "espnow";

EventGroupHandle_t s_espnow_event_group;
TaskHandle_t s_espnow_task_handle;
QueueHandle_t s_example_espnow_queue;

static uint32_t s_peer_start_times[DEFAULT_NUM_ROBOTS] = {0};
static int8_t s_last_rssi[DEFAULT_NUM_ROBOTS] = {0}; // Track per-robot RSSI
static uint32_t s_last_latency[DEFAULT_NUM_ROBOTS] = {0}; // Track per-robot latency (ms)


/* Throughput counting variables */
static uint32_t s_send_bytes = 0;
static uint32_t s_recv_bytes = 0;
static esp_timer_handle_t s_throughput_timer = NULL;

//CPU loggin params
static TaskStatus_t t[MAX_TASKS];
static uint32_t prev_idle0 = 0, prev_idle1 = 0;

//THIS NEEDS UPDATING MANUALLY FROM M5CORE2 MAC
static const uint8_t mac_addresses[DEFAULT_NUM_ROBOTS][ESP_NOW_ETH_ALEN] = {
    {0x78, 0x21, 0x84, 0x99, 0xDA, 0x8C},
    {0x78, 0x21, 0x84, 0x93, 0x78, 0xC0},
    {0x08, 0xB6, 0x1F, 0x88, 0x20, 0x04}
};

bool validate_mac_addresses_count() {
    int addresses_count = sizeof(mac_addresses) / sizeof(mac_addresses[0]);
    if (addresses_count > DEFAULT_NUM_ROBOTS) {
        ESP_LOGE(TAG, "Too many MAC addresses (%d) allowed = %d",
                 addresses_count, DEFAULT_NUM_ROBOTS);
        return false;
    }
    return true;
}

static int mac_addr_to_index(const uint8_t mac[ESP_NOW_ETH_ALEN]) {
    for (int i = 0; i < DEFAULT_NUM_ROBOTS; i++) {
        if (memcmp(mac_addresses[i], mac, ESP_NOW_ETH_ALEN) == 0) {
            return i;
        }
    }
    return -1;
}

// Check if hyper-mutation conditions are met
static void check_hyper_mutation(void)
{
    // 1) GA has run at least once
    // 2) GA not running (ga_ended == true)
    // 3) ga_buffer_queue is empty
    // 4) Only activate once every 3 seconds since ga finishes
    if (ga_has_run_before && ga_ended) {
        if (uxQueueMessagesWaiting(ga_buffer_queue) == 0) {
            uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
            if ((now_ms - s_last_ga_time) > 3000) {
                //ESP_LOGI(TAG, "Time gap: %lu ms", now_ms - s_last_ga_time);
                // Restart GA with hyper-mutation
                activate_hyper_mutation();
                ga_ended = false;
                xTaskCreatePinnedToCore(ga_task, "GA Task", 8192, NULL, 3, &ga_task_handle, 1);
            }
        }
    }
}

static void cpu_percent_to_string(char out[16])
{
    /* ----- 1 . Grab fresh kernel counters ------------------------ */
    UBaseType_t n = uxTaskGetSystemState(t, MAX_TASKS, NULL);         
    uint32_t idle0 = 0, idle1 = 0;
    for (UBaseType_t i = 0; i < n; ++i) {
        /* Idle tasks exist on both cores; their run-time == ‘slack time’ */
        if (!strcmp(t[i].pcTaskName, "IDLE0")) idle0 = t[i].ulRunTimeCounter;
        else if (!strcmp(t[i].pcTaskName, "IDLE1")) idle1 = t[i].ulRunTimeCounter;
    }

    /* ----- 2 . Convert “slack” into “CPU-used” for this 1-second slice -- */
    uint32_t d_idle0 = (idle0 - prev_idle0) & 0xFFFFFFFF;   // cheap wrap handling
    uint32_t d_idle1 = (idle1 - prev_idle1) & 0xFFFFFFFF;
    prev_idle0 = idle0;
    prev_idle1 = idle1;

    /* 100 % − idle%  = active%  (each SAMPLE_US == 100 %) */
    uint32_t pct0 = 100 - (d_idle0 * 100 / SAMPLE_US);
    uint32_t pct1 = 100 - (d_idle1 * 100 / SAMPLE_US);

    /* clamp in case the timer wasn’t exactly 1 s */
    if (pct0 > 100) pct0 = 100;
    if (pct1 > 100) pct1 = 100;

    /* ----- 3 . Emit “<Core0%>|<Core1%>” --------------------------- */
    snprintf(out, 16, "%u|%u", (unsigned)pct0, (unsigned)pct1);
}

/* Callback for 1-second throughput timer */
static void throughput_timer_cb(void *arg)
{

    //check if ga has gone stagnant
    check_hyper_mutation();

    // Calculate throughput in Kbps (bits/sec ÷ 1000)
    float kbps_in  = ((float)s_recv_bytes * 8.0f) / 1000.0f;
    float kbps_out = ((float)s_send_bytes * 8.0f) / 1000.0f;

    //debug heap
    // ESP_LOGI("HEAP", "free: %u, min-ever: %u",
    //     (unsigned int) esp_get_free_heap_size(),
    //     (unsigned int) esp_get_minimum_free_heap_size());

    // Log only if there’s any incoming/outgoing data
    if (s_send_bytes != 0 || s_recv_bytes != 0) {
        ESP_LOGI(TAG, "Throughput: In=%.2f Kbps, Out=%.2f Kbps", kbps_in, kbps_out);
        // Example: "T|12.34|56.78" T for throughput
        char log_type_buf[32];
        snprintf(log_type_buf, sizeof(log_type_buf), "%.2f|%.2f", kbps_in, kbps_out);

        event_log_t log_entry;
        time_t now = time(NULL);

        if (xSemaphoreTake(logCounterMutex, portMAX_DELAY)) {
            log_counter++;
            xSemaphoreGive(logCounterMutex);
        }

        log_entry.log_id       = log_counter;
        log_entry.log_datetime = now;
        strcpy(log_entry.status, "E"); //"E" for espnow
        strcpy(log_entry.tag, "L"); //"L" for local procres
        strcpy(log_entry.log_level, "T"); //"T" for throughput
        strlcpy(log_entry.log_type, log_type_buf, sizeof(log_entry.log_type));
        strcpy(log_entry.from_id, "");

        xQueueSend(LogQueue, &log_entry, portMAX_DELAY);

        if (s_recv_bytes != 0) {

            event_log_t rssi_entry;

            rssi_entry.log_id       = log_counter;
            rssi_entry.log_datetime = now; // same timestamp
            strcpy(rssi_entry.status, "E"); // E for espnow
            strcpy(rssi_entry.tag,    "L"); // L for local
            strcpy(rssi_entry.log_level, "C"); // C for connectivity
            // Example: "RSSI|<robot0>|<robot1>|<robot2>..."
            char rssi_buf[128] = "";
            char *ptr = rssi_buf;
            uint8_t own_mac[ESP_NOW_ETH_ALEN]; 
            esp_wifi_get_mac(ESP_IF_WIFI_STA, own_mac);
            for(int i = 0; i < DEFAULT_NUM_ROBOTS; i++){
                if (memcmp(mac_addresses[i], own_mac, ESP_NOW_ETH_ALEN) == 0) {
                    // Leave blank own RSSI
                    ptr += snprintf(ptr, rssi_buf + sizeof(rssi_buf) - ptr, "|");
                } else {
                    // Log RSSI
                    ptr += snprintf(ptr, rssi_buf + sizeof(rssi_buf) - ptr, "%d|", s_last_rssi[i]);
                }
            }
            strlcpy(rssi_entry.log_type, rssi_buf, sizeof(rssi_entry.log_type));
            strcpy(rssi_entry.from_id, "");

            xQueueSend(LogQueue, &rssi_entry, portMAX_DELAY);

        }

    }

    if(experiment_started){

        //Log CPU Usage
        event_log_t cpu_entry;
        time_t now = time(NULL);

        if (xSemaphoreTake(logCounterMutex, portMAX_DELAY)) {
            log_counter++;
            xSemaphoreGive(logCounterMutex);
        }

        cpu_entry.log_id       = log_counter;
        cpu_entry.log_datetime = now;
        strcpy(cpu_entry.status, "S"); // S for system
        strcpy(cpu_entry.tag,    "L"); // L for local
        strcpy(cpu_entry.log_level, "U"); // U for utilisation
        char cpu_buf[16];
        cpu_percent_to_string(cpu_buf);
        // Example: "<Core0%>|<Core1%>"
        strlcpy(cpu_entry.log_type, cpu_buf, sizeof(cpu_entry.log_type));
        strcpy(cpu_entry.from_id, "");

        xQueueSend(LogQueue, &cpu_entry, portMAX_DELAY);

    }

    // Reset counters
    s_send_bytes = 0;
    s_recv_bytes = 0;
}

/* ESPNOW sending or receiving callback function is called in WiFi task.
 * Users should not do lengthy operations from this task. Instead, post
 * necessary data to a queue and handle it from a lower priority task. */
static void example_espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    example_espnow_event_t evt;
    example_espnow_event_send_cb_t *send_cb = &evt.info.send_cb;

    if (mac_addr == NULL) {
        ESP_LOGE(TAG, "Send cb arg error");
        return;
    }

    int idx = mac_addr_to_index(mac_addr);
    if (idx >= 0) {
        send_cb->start_time_ms = s_peer_start_times[idx];
        uint32_t ack_time_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        uint32_t latency_ms = ack_time_ms - send_cb->start_time_ms;
        s_last_latency[idx] = latency_ms; // Store last measured latency by mac
        send_cb->latency_ms = latency_ms;
    } else {
        send_cb->start_time_ms = 0;
        send_cb->latency_ms = 0;
    }

    evt.id = EXAMPLE_ESPNOW_SEND_CB;
    memcpy(send_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    send_cb->status = status;

    if (xQueueSend(s_example_espnow_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Send send queue fail");
    }

    if (status == ESP_NOW_SEND_SUCCESS) {
        s_send_bytes += sizeof(out_message_t); //For throughput calc
    }

}

static void example_espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    example_espnow_event_t evt;
    example_espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;
    uint8_t * mac_addr = recv_info->src_addr;
    int8_t rssi = recv_info->rx_ctrl->rssi; //get RSSI
    //uint8_t * des_addr = recv_info->des_addr;

    if (mac_addr == NULL || data == NULL || len <= 0) {
        ESP_LOGE(TAG, "Receive cb arg error");
        return;
    }

    evt.id = EXAMPLE_ESPNOW_RECV_CB;
    memcpy(recv_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    recv_cb->data = malloc(len);
    if (recv_cb->data == NULL) {
        //TODO: Log ERROR RATE?
        ESP_LOGE(TAG, "Malloc receive data fail");
        return;
    }

    for(int i=0; i<DEFAULT_NUM_ROBOTS; i++){
        if(memcmp(mac_addresses[i], mac_addr, ESP_NOW_ETH_ALEN) == 0){
            s_last_rssi[i] = rssi;
            break;
        }
    }

    memcpy(recv_cb->data, data, len);
    recv_cb->data_len = len;
    s_recv_bytes += len; //For throughput calc
    if (xQueueSend(s_example_espnow_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Send receive queue fail");
        free(recv_cb->data);
    }
}

/* Parse received ESPNOW data. */
static int parse_out_message(const uint8_t *data, int len, out_message_t *msg_out)
{
    if (len != sizeof(out_message_t)) {
        ESP_LOGE(TAG, "parse_out_message: invalid size. Expected %d, got %d",
                 (int)sizeof(out_message_t), len);
        return -1;
    }
    //copy bytes directly into out_message_t
    memcpy(msg_out, data, sizeof(out_message_t));
    msg_out->robot_id[sizeof(msg_out->robot_id) - 1] = '\0';
    return 0;
}

int example_espnow_data_parse(uint8_t *data, uint16_t data_len, uint8_t *state, uint16_t *seq, uint32_t *magic)
{
    example_espnow_data_t *buf = (example_espnow_data_t *)data;
    uint16_t crc, crc_cal = 0;

    if (data_len < sizeof(example_espnow_data_t)) {
        ESP_LOGE(TAG, "Receive ESPNOW data too short, len:%d", data_len);
        return -1;
    }

    *state = buf->state;
    *seq = buf->seq_num;
    *magic = buf->magic;
    crc = buf->crc;
    buf->crc = 0;
    crc_cal = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, data_len);

    if (crc_cal == crc) {
        return buf->type;
    }

    return -1;
}

/* Prepare ESPNOW data to be sent. */
void espnow_push_best_solution(float current_best_fitness, const float *best_solution,
    size_t gene_count, uint32_t log_id, time_t created_datetime)
{
    // Prepare the out_message structure
    out_message_t out_msg;
    memset(&out_msg, 0, sizeof(out_message_t));

    out_msg.log_id = log_id;
    out_msg.created_datetime = created_datetime;

    // Use last two MAC bytes to build a 5-digit ID
    strncpy(out_msg.robot_id, robot_id, sizeof(out_msg.robot_id) - 1);
    out_msg.robot_id[sizeof(out_msg.robot_id) - 1] = '\0';  //ensure null-termination

    //example: "25.123| 20.111| 35.112| ..."
    int offset = 0;
    offset += snprintf(out_msg.message + offset, sizeof(out_msg.message) - offset, "%.3f|", current_best_fitness);
    for (size_t i = 0; i < gene_count && offset < (int)sizeof(out_msg.message); i++) {
        offset += snprintf(out_msg.message + offset, sizeof(out_msg.message) - offset, "%.3f|", best_solution[i]);
    }

    //get own MAC
    uint8_t own_mac[ESP_NOW_ETH_ALEN];
    esp_wifi_get_mac(ESP_IF_WIFI_STA, own_mac);

    // Prepare a local copy of mac_addresses for shuffling or ranking if needed
    uint8_t macs[DEFAULT_NUM_ROBOTS][ESP_NOW_ETH_ALEN];
    memcpy(macs, mac_addresses, sizeof(macs));

    // COMM_AWARE mode: rank peers by latency/RSSI (worst first)
    #if DEFAULT_TOPOLOGY == TOPOLOGY_COMM_AWARE // "COMM_AWARE"
    typedef struct {
        uint8_t mac[ESP_NOW_ETH_ALEN];
        int8_t rssi;
        uint32_t latency;
        int null_metric; // 1 if rssi or latency is null
        float score; // for sorting
        int orig_idx;
    } comm_peer_t;
    comm_peer_t peers[DEFAULT_NUM_ROBOTS];
    for (int i = 0; i < DEFAULT_NUM_ROBOTS; i++) {
        memcpy(peers[i].mac, macs[i], ESP_NOW_ETH_ALEN);
        peers[i].rssi = s_last_rssi[i];
        peers[i].latency = s_last_latency[i]; // Use actual last measured latency
        peers[i].orig_idx = i;
        // Null if rssi is -128 (never received) or latency is 0 (never sent)
        peers[i].null_metric = (peers[i].rssi == -128 || peers[i].latency == 0);
    }
    // Normalise and score: higher score = worse
    int8_t min_rssi = 127, max_rssi = -128;
    uint32_t min_lat = UINT32_MAX, max_lat = 0;
    for (int i = 0; i < DEFAULT_NUM_ROBOTS; i++) {
        if (!peers[i].null_metric) {
            if (peers[i].rssi < min_rssi) min_rssi = peers[i].rssi;
            if (peers[i].rssi > max_rssi) max_rssi = peers[i].rssi;
            if (peers[i].latency < min_lat) min_lat = peers[i].latency;
            if (peers[i].latency > max_lat) max_lat = peers[i].latency;
        }
    }
    for (int i = 0; i < DEFAULT_NUM_ROBOTS; i++) {
        if (peers[i].null_metric) {
            peers[i].score = 1e6f; // Highest priority
        } else {
            float norm_rssi = (max_rssi != min_rssi) ? (float)(max_rssi - peers[i].rssi) / (max_rssi - min_rssi) : 0.0f;
            float norm_lat = (max_lat != min_lat) ? (float)(peers[i].latency - min_lat) / (max_lat - min_lat) : 0.0f;
            peers[i].score = norm_rssi + norm_lat; // Simple sum, can be weighted
        }
    }
    // Sort: null_metric first, then by score descending (worst first)
    for (int i = 0; i < DEFAULT_NUM_ROBOTS - 1; i++) {
        for (int j = i + 1; j < DEFAULT_NUM_ROBOTS; j++) {
            if (peers[i].score < peers[j].score) {
                comm_peer_t tmp = peers[i];
                peers[i] = peers[j];
                peers[j] = tmp;
            }
        }
    }
    // Overwrite macs with sorted order
    for (int i = 0; i < DEFAULT_NUM_ROBOTS; i++) {
        memcpy(macs[i], peers[i].mac, ESP_NOW_ETH_ALEN);
    }
    #elif DEFAULT_TOPOLOGY == TOPOLOGY_RANDOM // "RANDOM"
    // Fisher-Yates shuffle using esp_random()
    for (int i = DEFAULT_NUM_ROBOTS - 1; i > 0; i--) {
        uint32_t r = esp_random() % (i + 1);
        uint8_t tmp[ESP_NOW_ETH_ALEN];
        memcpy(tmp, macs[i], ESP_NOW_ETH_ALEN);
        memcpy(macs[i], macs[r], ESP_NOW_ETH_ALEN);
        memcpy(macs[r], tmp, ESP_NOW_ETH_ALEN);
    }
    #endif
    //unicast message to each registered peer except self
    for (int i = 0; i < DEFAULT_NUM_ROBOTS; i++) {
        if (memcmp(macs[i], own_mac, ESP_NOW_ETH_ALEN) == 0) {
            continue; // skip sending to self
        }
        int idx = mac_addr_to_index(macs[i]);
        if (idx >= 0) {
            uint32_t current_time_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
            s_peer_start_times[idx] = current_time_ms;
        }
        esp_err_t err = esp_now_send(macs[i], (uint8_t *)&out_msg, sizeof(out_msg));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to send best solution to " MACSTR ": %s",
                MAC2STR(macs[i]), esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Sending best solution to " MACSTR, MAC2STR(macs[i]));
        }
    }
}

static void log_incoming_buffer_message(const out_message_t *incoming_msg)
{
    event_log_t log_entry;
    time_t now = time(NULL);

    if (xSemaphoreTake(logCounterMutex, portMAX_DELAY)) {
        log_counter++;
        xSemaphoreGive(logCounterMutex);
    }

    log_entry.log_id = log_counter;
    log_entry.log_datetime = now;
    strcpy(log_entry.status, "M");// "M" for message
    strcpy(log_entry.tag, "R");   // "R" for recieved
    strcpy(log_entry.log_level, "I"); // Info level
    strcpy(log_entry.log_type, "B");  // "B" for buffer
    strlcpy(log_entry.from_id, incoming_msg->robot_id, sizeof(log_entry.from_id));

    xQueueSend(LogQueue, &log_entry, portMAX_DELAY);

}

static void log_local_evaluation(float remote_best_fitness, float local_best_fitness, const char *remote_robot_id)
{
    event_log_t eval_log;
    time_t now = time(NULL);

    // Increment the global log counter
    if (xSemaphoreTake(logCounterMutex, portMAX_DELAY)) {
        log_counter++;
        xSemaphoreGive(logCounterMutex);
    }

    eval_log.log_id       = log_counter;
    eval_log.log_datetime = now;
    strcpy(eval_log.status, "G");  // Genetic algo
    strcpy(eval_log.tag,    "L");  // Local process
    strcpy(eval_log.log_level, "I");  // Info
    strlcpy(eval_log.from_id, remote_robot_id, sizeof(eval_log.from_id));

    // "A" = accepted (remote is better), "R" = rejected
    if (remote_best_fitness < local_best_fitness) {
        strcpy(eval_log.log_type, "A"); // A for accept migration
    } else {
        strcpy(eval_log.log_type, "R"); // R for reject migration
    }

    xQueueSend(LogQueue, &eval_log, portMAX_DELAY);
}

void drain_buffered_messages(void)
{
    example_espnow_event_t tmp_evt;
    out_message_t incoming_msg;
    float best_remote_fitness = FLT_MAX;
    float best_remote_genes[MAX_GENES] = {0};
    char best_robot_id[sizeof(incoming_msg.robot_id)] = {0};
    bool candidate_found = false;

    /* Drain all buffered messages in ga_buffer_queue */
    while (xQueueReceive(ga_buffer_queue, &tmp_evt, 0) == pdTRUE) {
        example_espnow_event_recv_cb_t *buffered_recv_cb = &tmp_evt.info.recv_cb;
        if (parse_out_message(buffered_recv_cb->data, buffered_recv_cb->data_len, &incoming_msg) == 0) {
            ESP_LOGI(TAG, "Processed buffered message from %s", incoming_msg.robot_id);
            log_incoming_buffer_message(&incoming_msg);

            // Extract remote fitness and genes:
            float remote_candidate = 0.0f;
            float remote_candidate_genes[MAX_GENES] = {0};
            char msg_copy[sizeof(incoming_msg.message)];
            strncpy(msg_copy, incoming_msg.message, sizeof(msg_copy));
            msg_copy[sizeof(msg_copy) - 1] = '\0';
            char *token = strtok(msg_copy, "|");
            if (token) {
                remote_candidate = atof(token); // first token is best fitness
                int gene_idx = 0;
                while ((token = strtok(NULL, "|")) != NULL && gene_idx < MAX_GENES) {
                    remote_candidate_genes[gene_idx++] = atof(token);
                }
            }
            if (remote_candidate < best_remote_fitness) {
                best_remote_fitness = remote_candidate;
                memcpy(best_remote_genes, remote_candidate_genes, sizeof(best_remote_genes));
                strncpy(best_robot_id, incoming_msg.robot_id, sizeof(best_robot_id) - 1);
                candidate_found = true;
            }
        } else {
            ESP_LOGW(TAG, "Failed to parse buffered msg");
        }
        free(buffered_recv_cb->data);
    }

    /* If a better remote candidate is found, integrate it. */
    if (candidate_found) {
        float local_best_fitness = ((int)(ga_get_local_best_fitness() * 1000)) / 1000.0f;
        if (best_remote_fitness < local_best_fitness) {

            ESP_LOGI(TAG, "Best buffered remote solution %.3f from %s is better than local %.3f, re-initializing local GA.",
                     best_remote_fitness, best_robot_id, local_best_fitness);

            log_local_evaluation(best_remote_fitness, local_best_fitness, best_robot_id);
            ga_integrate_remote_solution(best_remote_genes);
            ga_ended = false;
            xTaskCreatePinnedToCore(ga_task, "GA Task", 8192, NULL, 3, &ga_task_handle, 1);
        } else {
            ESP_LOGW(TAG, "Best buffered remote solution %.3f from %s is not better than local %.3f, ignoring.",
                     best_remote_fitness, best_robot_id, local_best_fitness);
        }
    }
}

void espnow_task(void *pvParameter)
{
    example_espnow_event_t evt;
    out_message_t incoming_msg;

    vTaskDelay(pdMS_TO_TICKS(100));

    for (;;) {

        // Block until we receive something from the queue
        if (xQueueReceive(s_example_espnow_queue, &evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        // By default the first message that arrives will be processed.
        switch (evt.id) {
            case EXAMPLE_ESPNOW_STOP:
            {   
                ga_ended = true;
                
                    /* ---- wait for GA_COMPLETED_BIT, discarding ESPNOW events -------- */
                    for (;;) {
                        /* 1.  Is GA finished yet? */
                        if (xEventGroupGetBits(ga_event_group) & GA_COMPLETED_BIT) {
                            break;                             /* done waiting */
                        }
                        /* 2.  Drain anything that may have been queued */
                        while (xQueueReceive(s_example_espnow_queue, &evt, 0) == pdTRUE) {
                            /* Free dynamically-allocated buffers to avoid leaks */
                            if (evt.id == EXAMPLE_ESPNOW_RECV_CB &&
                                evt.info.recv_cb.data) {
                                free(evt.info.recv_cb.data);
                            }
                        }
                        /* 3.  Sleep a little so we don't burn the CPU */
                        vTaskDelay(pdMS_TO_TICKS(50));
                    }

                xEventGroupSetBits(s_espnow_event_group, ESPNOW_COMPLETED_BIT);
                ESP_LOGI(TAG, "Stopping ESPNOW task");
                vTaskDelete(NULL);
                break;
            }
            case EXAMPLE_ESPNOW_RECV_CB:
            {
                example_espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;
                //check if GA is still running
                if (ga_event_group && !(xEventGroupGetBits(ga_event_group) & GA_COMPLETED_BIT)) {
                    ESP_LOGI(TAG, "GA still running; buffering received message.");
                    // Buffer the message for later processing.
                    if (xQueueSend(ga_buffer_queue, &evt, 0) != pdTRUE) {
                        ESP_LOGW(TAG, "Failed to buffer message; dropping it.");
                        free(recv_cb->data);
                    }
                    break;
                } 
                
                example_espnow_event_t current_evt = evt;

                //Process current message
                if (parse_out_message(current_evt.info.recv_cb.data, current_evt.info.recv_cb.data_len, &incoming_msg) == 0) {
                    ESP_LOGI(TAG, "Received message from %s" , incoming_msg.robot_id);

                    event_log_t log_entry;
                    time_t now = time(NULL);

                    if (xSemaphoreTake(logCounterMutex, portMAX_DELAY)) {
                        log_counter++;
                        xSemaphoreGive(logCounterMutex);
                    }

                    log_entry.log_id = log_counter;
                    log_entry.log_datetime = now;
                    strcpy(log_entry.status, "E"); // E for esp-now
                    strcpy(log_entry.tag, "M"); // M for message
                    strcpy(log_entry.log_level, "I"); //I for information
                    strcpy(log_entry.log_type, "R"); // R for recieve
                    strlcpy(log_entry.from_id, incoming_msg.robot_id, sizeof(incoming_msg.robot_id));
                    xQueueSend(LogQueue, &log_entry, portMAX_DELAY);

                    //parse the | delimited message to extract remote_best_fitness and remote genes
                    //    Example: "25.123| 1.500| 2.300| ..."  
                    float remote_best_fitness = 0.0f;
                    float remote_genes[MAX_GENES] = {0};

                    //safely use strtok
                    char msg_copy[sizeof(incoming_msg.message)];
                    strncpy(msg_copy, incoming_msg.message, sizeof(msg_copy));
                    msg_copy[sizeof(msg_copy) - 1] = '\0';
                    char *token = strtok(msg_copy, "|"); 
                    if (token) {
                        remote_best_fitness = atof(token); //first token is the best fitness
                    }
                    //subsequent tokens are gene values
                    int gene_idx = 0;
                    while ((token = strtok(NULL, "|")) != NULL && gene_idx < MAX_GENES) {
                        remote_genes[gene_idx++] = atof(token);
                    }

                    //get local fitness for comparison from ga.
                    float local_best_fitness = ((int)(ga_get_local_best_fitness() * 1000)) / 1000.0f;
                    if (remote_best_fitness < local_best_fitness) {
                        
                        ESP_LOGI(TAG, "Remote solution %.3f is better than local %.3f, re-initializing local GA.",
                                remote_best_fitness, local_best_fitness);

                        log_local_evaluation(remote_best_fitness, local_best_fitness, incoming_msg.robot_id);

                        ga_integrate_remote_solution(remote_genes);
                        //re-init ga_task
                        ga_ended = false;
                        xTaskCreatePinnedToCore(ga_task, "GA Task", 8192, NULL, 3, &ga_task_handle, 1);
                       
                    }

                } else {
                    ESP_LOGW(TAG, "Failed to parse incoming msg, ignoring packet");
                }

                // Cleanup
                free(current_evt.info.recv_cb.data);
                break;
            }

            case EXAMPLE_ESPNOW_SEND_CB:
            {
                example_espnow_event_send_cb_t *send_cb = &evt.info.send_cb;

                // uint32_t ack_time_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
                // uint32_t latency_ms = ack_time_ms - send_cb->start_time_ms;
                uint32_t latency_ms = send_cb->latency_ms;

                char short_id[5] = {0};  // 2 bytes in hex + 2 digits + null terminator
                sprintf(short_id, "%02X%02X", send_cb->mac_addr[4], send_cb->mac_addr[5]);


                event_log_t log_entry;

                time_t now = time(NULL);

                if (xSemaphoreTake(logCounterMutex, portMAX_DELAY)) {
                    log_counter++;
                    xSemaphoreGive(logCounterMutex);
                }

                //LATENCY & PACKET LOSS
                // Use ‘O’ for OK, ‘F’ for FAIL, and include |latency|send|robot_id
                char status_char = (send_cb->status == ESP_NOW_SEND_SUCCESS) ? 'O' : 'F';
                char temp_log_type[32];
                snprintf(temp_log_type, sizeof(temp_log_type), "%c|%u|%s",
                        status_char,
                        (unsigned)latency_ms,
                        short_id);

                //INTERNAL SEND LOG
                log_entry.log_id       = log_counter;
                log_entry.log_datetime = now;
                strcpy(log_entry.status, "E"); // E for ESPNOW
                strcpy(log_entry.tag, "M"); // M for message
                strcpy(log_entry.log_level, "I"); //I for information
                strlcpy(log_entry.log_type, temp_log_type, sizeof(log_entry.log_type));
                strcpy(log_entry.from_id, ""); // blank as send
            
                xQueueSend(LogQueue, &log_entry, portMAX_DELAY);

                break;
            }

            default:
                ESP_LOGE(TAG, "Callback type error: %d", evt.id);
                break;
        }

    }

}

esp_err_t espnow_init(void)
{
    uint8_t own_mac[ESP_NOW_ETH_ALEN];
    ESP_ERROR_CHECK(esp_wifi_get_mac(ESP_IF_WIFI_STA, own_mac));  // Get self MAC address

    s_example_espnow_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(example_espnow_event_t));
    if (s_example_espnow_queue == NULL) {
        ESP_LOGE(TAG, "Create mutex fail");
        return ESP_FAIL;
    }

    ga_buffer_queue = xQueueCreate(25, sizeof(example_espnow_event_t));
    if (ga_buffer_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create ga_buffer_queue");
        return ESP_FAIL;
    }

    //TODO: Currently this does not work as it needs to be on same channel as AP router
    //ESP_ERROR_CHECK( esp_wifi_set_channel(CONFIG_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
    //Enable long range
    #if CONFIG_ESPNOW_ENABLE_LONG_RANGE
    ESP_ERROR_CHECK( esp_wifi_set_protocol(ESPNOW_WIFI_IF, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N|WIFI_PROTOCOL_LR) );
    #endif

    /* Initialize ESPNOW and register sending and receiving callback function. */
    ESP_ERROR_CHECK( esp_now_init() );
    ESP_ERROR_CHECK( esp_now_register_send_cb(example_espnow_send_cb) );
    ESP_ERROR_CHECK( esp_now_register_recv_cb(example_espnow_recv_cb) );
    #if CONFIG_ESPNOW_ENABLE_POWER_SAVE
    ESP_ERROR_CHECK( esp_now_set_wake_window(CONFIG_ESPNOW_WAKE_WINDOW) );
    ESP_ERROR_CHECK( esp_wifi_connectionless_module_set_wake_interval(CONFIG_ESPNOW_WAKE_INTERVAL) );
    #endif

    /* Add peers: register all other robot MAC addresses */
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL) {
        ESP_LOGE(TAG, "Malloc peer information fail");
        vSemaphoreDelete(s_example_espnow_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }

    for (int i = 0; i < DEFAULT_NUM_ROBOTS; i++) {
        if (memcmp(mac_addresses[i], own_mac, ESP_NOW_ETH_ALEN) == 0) {
            ESP_LOGI("espnow", "Skipping adding self to ESP-NOW peers.");
            continue;  // Skip adding if the MAC address matches the device's own MAC
        }

        memset(peer, 0, sizeof(esp_now_peer_info_t));
        peer->channel = CONFIG_ESPNOW_CHANNEL;
        peer->ifidx = ESPNOW_WIFI_IF;
        peer->encrypt = false;
        memcpy(peer->peer_addr, mac_addresses[i], ESP_NOW_ETH_ALEN); //Register Peer
        if (esp_now_add_peer(peer) != ESP_OK) {
            ESP_LOGE("espnow", "Failed to add peer: %02x:%02x:%02x:%02x:%02x:%02x",
                     mac_addresses[i][0], mac_addresses[i][1], mac_addresses[i][2],
                     mac_addresses[i][3], mac_addresses[i][4], mac_addresses[i][5]);
        }
    }
    free(peer);

    //kpi timer
    const esp_timer_create_args_t throughput_timer_args = {
        .callback = &throughput_timer_cb,
        .name = "throughput_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&throughput_timer_args, &s_throughput_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_throughput_timer, 1000000UL));

    ESP_LOGI(TAG, "ESPNOW base initialization complete.");
    return ESP_OK;
}

void espnow_deinit_all(void)
{
    ESP_LOGI(TAG, "De-initializing ESPNOW and stopping throughput timer");

    if (!s_example_espnow_queue && !s_espnow_event_group && !s_throughput_timer) {
        ESP_LOGW(TAG, "espnow_deinit_all() called twice; ignoring");
        return;
    }

    // Stop and delete the throughput timer if it exists
    if (s_throughput_timer) {
        esp_timer_stop(s_throughput_timer);
        esp_timer_delete(s_throughput_timer);
        s_throughput_timer = NULL;
    }

    // Deinitialize ESPNOW
    esp_err_t err = esp_now_deinit();
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_NOT_INIT) {
        ESP_LOGW(TAG, "esp_now_deinit failed: %s", esp_err_to_name(err));
    }

    // Delete the ESPNOW queue if exists
    if (s_example_espnow_queue) {
        /* 1.  Drain anything a late ISR / task might still push */
        example_espnow_event_t dummy;
        while (xQueueReceive(s_example_espnow_queue, &dummy, 0) == pdTRUE) {
            /* If the event contains a malloc-ed buffer, free it here */
            if (dummy.id == EXAMPLE_ESPNOW_RECV_CB &&
                dummy.info.recv_cb.data) {
                free(dummy.info.recv_cb.data);
            }
        }
        /* 2.  Now it is safe to delete the queue handle */
        vQueueDelete(s_example_espnow_queue);
        s_example_espnow_queue = NULL;
    }

    // Delete the event group if exists
    if (s_espnow_event_group) {
        vEventGroupDelete(s_espnow_event_group);
        s_espnow_event_group = NULL;
    }
}