/* ESPNOW Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

/*
   This example shows how to use ESPNOW.
   Prepare two device, one for sending ESPNOW data and another for receiving
   ESPNOW data.
*/
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include <float.h>
#include "freertos/FreeRTOS.h"
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

static QueueHandle_t s_example_espnow_queue;

#define NUM_ROBOTS 2  // Number of robots in the swarm, max 20

static const uint8_t mac_addresses[NUM_ROBOTS][ESP_NOW_ETH_ALEN] = {
    {0x78, 0x21, 0x84, 0x99, 0xDA, 0x8C},
    {0x78, 0x21, 0x84, 0x93, 0x78, 0xC0}
};

//static uint8_t s_example_broadcast_mac[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static uint16_t s_example_espnow_seq[EXAMPLE_ESPNOW_DATA_MAX] = { 0, 0 };

static void example_espnow_deinit(example_espnow_send_param_t *send_param);


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

    evt.id = EXAMPLE_ESPNOW_SEND_CB;
    memcpy(send_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    send_cb->status = status;
    if (xQueueSend(s_example_espnow_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Send send queue fail");
    }
}

static void example_espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    example_espnow_event_t evt;
    example_espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;
    uint8_t * mac_addr = recv_info->src_addr;
    //uint8_t * des_addr = recv_info->des_addr;

    if (mac_addr == NULL || data == NULL || len <= 0) {
        ESP_LOGE(TAG, "Receive cb arg error");
        return;
    }

    // if (IS_BROADCAST_ADDR(des_addr)) {
    //     /* If added a peer with encryption before, the receive packets may be
    //      * encrypted as peer-to-peer message or unencrypted over the broadcast channel.
    //      * Users can check the destination address to distinguish it.
    //      */
    //     ESP_LOGD(TAG, "Receive broadcast ESPNOW data");
    // } else {
    //     ESP_LOGD(TAG, "Receive unicast ESPNOW data");
    // }

    evt.id = EXAMPLE_ESPNOW_RECV_CB;
    memcpy(recv_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    recv_cb->data = malloc(len);
    if (recv_cb->data == NULL) {
        ESP_LOGE(TAG, "Malloc receive data fail");
        return;
    }
    memcpy(recv_cb->data, data, len);
    recv_cb->data_len = len;
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

    //unicast message to each registered peer except self
    for (int i = 0; i < NUM_ROBOTS; i++) {
        if (memcmp(mac_addresses[i], own_mac, ESP_NOW_ETH_ALEN) == 0) {
        continue; // skip sending to self
        }
        esp_err_t err = esp_now_send(mac_addresses[i], (uint8_t *)&out_msg, sizeof(out_msg));
        if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send best solution to " MACSTR ": %s",
        MAC2STR(mac_addresses[i]), esp_err_to_name(err));
        } else {
        ESP_LOGI(TAG, "Sent best solution to " MACSTR, MAC2STR(mac_addresses[i]));
        }
    }
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
        float local_best_fitness = ga_get_local_best_fitness();
        if (best_remote_fitness < local_best_fitness) {
            ESP_LOGI(TAG, "Best buffered remote solution %.3f from %s is better than local %.3f, re-initializing local GA.",
                     best_remote_fitness, best_robot_id, local_best_fitness);
            ga_integrate_remote_solution(best_remote_genes);
            ga_ended = false;
            xTaskCreatePinnedToCore(ga_task, "GA Task", 4096, NULL, 5, &ga_task_handle, 1);
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
        //ESPNOW_COMPLETED_BIT signals time to stop
        EventBits_t bits = xEventGroupGetBits(s_espnow_event_group);
        if (bits & ESPNOW_COMPLETED_BIT) {
            ESP_LOGI(TAG, "Shutting down ESPNOW...");
            break;
        }

        // Block until we receive something from the queue
        if (xQueueReceive(s_example_espnow_queue, &evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        // By default the first message that arrives will be processed.
        switch (evt.id) {
            //TODO: explain this callback and immigrank_K logic
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

                    // if (xSemaphoreTake(logCounterMutex, portMAX_DELAY)) {
                    //     log_counter++;
                    //     xSemaphoreGive(logCounterMutex);
                    // }

                    log_entry.log_id = log_counter;
                    log_entry.log_datetime = now;
                    log_entry.status = strdup("E"); // E for esp-now
                    log_entry.tag = strdup("M"); // M for message
                    log_entry.log_level = strdup("I"); //I for information
                    log_entry.log_type = strdup("F"); // F for fitness
                    log_entry.from_id = strdup(incoming_msg.robot_id);
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
                    float local_best_fitness = ga_get_local_best_fitness();
                    if (remote_best_fitness < local_best_fitness) {
                        ESP_LOGI(TAG, "Remote solution %.3f is better than local %.3f, re-initializing local GA.",
                                 remote_best_fitness, local_best_fitness);

                        ga_integrate_remote_solution(remote_genes);
                        //re-init ga_task
                        ga_ended = false;
                        xTaskCreatePinnedToCore(ga_task, "GA Task", 4096, NULL, 5, &ga_task_handle, 1);
                       
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
                //TODO: Might need a cb for sending messages?
                break;
            }

            default:
                ESP_LOGE(TAG, "Callback type error: %d", evt.id);
                break;
        }
    }

    //free up resources before closing
    if (s_example_espnow_queue) {
        vQueueDelete(s_example_espnow_queue);
        s_example_espnow_queue = NULL;
    }

    esp_now_deinit();
    vTaskDelete(NULL);
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

    ga_buffer_queue = xQueueCreate(10, sizeof(example_espnow_event_t));
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

    for (int i = 0; i < NUM_ROBOTS; i++) {
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
    ESP_LOGI(TAG, "ESPNOW base initialization complete.");
    return ESP_OK;
}

// void espnow_deinit_task(void *pvParameter)
// {
//     vTaskDelay(pdMS_TO_TICKS(60000));
//     ESP_LOGI(TAG, "Signaling ESPNOW_COMPLETED_BIT to end experiment...");
//     xEventGroupSetBits(s_espnow_event_group, ESPNOW_COMPLETED_BIT);

//     vTaskDelete(NULL);
// }