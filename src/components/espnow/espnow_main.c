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
void example_espnow_data_prepare(example_espnow_send_param_t *send_param)
{
    example_espnow_data_t *buf = (example_espnow_data_t *)send_param->buffer;

    assert(send_param->len >= sizeof(example_espnow_data_t));

    //buf->type = IS_BROADCAST_ADDR(send_param->dest_mac) ? EXAMPLE_ESPNOW_DATA_BROADCAST : EXAMPLE_ESPNOW_DATA_UNICAST;
    buf->type = EXAMPLE_ESPNOW_DATA_UNICAST;
    buf->state = send_param->state;
    buf->seq_num = s_example_espnow_seq[buf->type]++;
    buf->crc = 0;
    buf->magic = send_param->magic;
    /* Fill all remaining bytes after the data with random values */
    esp_fill_random(buf->payload, send_param->len - sizeof(example_espnow_data_t));
    buf->crc = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, send_param->len);
}

static void example_espnow_task(void *pvParameter)
{
    example_espnow_event_t evt;
    uint8_t recv_state = 0;
    uint16_t recv_seq = 0;
    uint32_t recv_magic = 0;
    espnow_lvgl_message_t msg;
    //bool is_broadcast = false;
    int ret;

    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGI(TAG, "Start sending data to peers...");
    example_espnow_send_param_t *send_param = (example_espnow_send_param_t *)pvParameter;
    if (esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK) {
        ESP_LOGE(TAG, "Send error");
        example_espnow_deinit(send_param);
        vTaskDelete(NULL);
    }

    // /* Start sending broadcast ESPNOW data. */
    // example_espnow_send_param_t *send_param = (example_espnow_send_param_t *)pvParameter;
    // if (esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK) {
    //     ESP_LOGE(TAG, "Send error");
    //     example_espnow_deinit(send_param);
    //     vTaskDelete(NULL);
    // }

    while (xQueueReceive(s_example_espnow_queue, &evt, portMAX_DELAY) == pdTRUE) {
        switch (evt.id) {
            case EXAMPLE_ESPNOW_SEND_CB:
            {
                example_espnow_event_send_cb_t *send_cb = &evt.info.send_cb;
                //is_broadcast = IS_BROADCAST_ADDR(send_cb->mac_addr);

                //ESP_LOGD(TAG, "Sending data to "MACSTR", status1: %d", MAC2STR(send_cb->mac_addr), send_cb->status);

                // if (is_broadcast && (send_param->broadcast == false)) {
                //     break;
                // }

                //if (!is_broadcast) {
                send_param->count--;
                if (send_param->count == 0) {
                    ESP_LOGI(TAG, "Send done");
                    experiment_ended = true;
                    example_espnow_deinit(send_param);
                    xEventGroupSetBits(s_espnow_event_group, ESPNOW_COMPLETED_BIT);
                    vTaskDelete(NULL);
                }
                //}

                /* Delay a while before sending the next data. */
                if (send_param->delay > 0) {
                    vTaskDelay(pdMS_TO_TICKS(send_param->delay));
                }

                memcpy(send_param->dest_mac, send_cb->mac_addr, ESP_NOW_ETH_ALEN);
                example_espnow_data_prepare(send_param);

                /* Send the next data after the previous data is sent. */
                if (esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK) {
                    ESP_LOGE(TAG, "Send error");
                    example_espnow_deinit(send_param);
                    vTaskDelete(NULL);
                }

                if(send_param->state == 0){
                    ESP_LOGI(TAG, "Connecting to "MACSTR"", MAC2STR(send_cb->mac_addr));
                    snprintf(msg.message, sizeof(msg.message), "Connecting to "MACSTR"",
                    MAC2STR(send_cb->mac_addr));
                    lv_label_set_text(espnow_label, msg.message);
                } else {
                    ESP_LOGI(TAG, "Sending to "MACSTR"", MAC2STR(send_cb->mac_addr));
                    snprintf(msg.message, sizeof(msg.message), "Sending to "MACSTR"",
                    MAC2STR(send_cb->mac_addr));
                    lv_label_set_text(espnow_label, msg.message);
                }

                break;
            }
            case EXAMPLE_ESPNOW_RECV_CB:
            {
                example_espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;

                ret = example_espnow_data_parse(recv_cb->data, recv_cb->data_len, &recv_state, &recv_seq, &recv_magic);
                free(recv_cb->data);
                // if (ret == EXAMPLE_ESPNOW_DATA_BROADCAST) {
                //     ESP_LOGI(TAG, "Receive %dth broadcast data from: "MACSTR", len: %d", recv_seq, MAC2STR(recv_cb->mac_addr), recv_cb->data_len);

                //     //TODO: Consider whether we need this in unicast mode instead!
                //     /* If MAC address does not exist in peer list, add it to peer list. */
                //     // if (esp_now_is_peer_exist(recv_cb->mac_addr) == false) {
                //     //     esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
                //     //     if (peer == NULL) {
                //     //         ESP_LOGE(TAG, "Malloc peer information fail");
                //     //         example_espnow_deinit(send_param);
                //     //         vTaskDelete(NULL);
                //     //     }
                //     //     memset(peer, 0, sizeof(esp_now_peer_info_t));
                //     //     peer->channel = CONFIG_ESPNOW_CHANNEL;
                //     //     peer->ifidx = ESPNOW_WIFI_IF;
                //     //     peer->encrypt = true;
                //     //     memcpy(peer->lmk, CONFIG_ESPNOW_LMK, ESP_NOW_KEY_LEN);
                //     //     memcpy(peer->peer_addr, recv_cb->mac_addr, ESP_NOW_ETH_ALEN);
                //     //     ESP_ERROR_CHECK( esp_now_add_peer(peer) );
                //     //     free(peer);
                //     // }

                    // /* Indicates that the device has received broadcast ESPNOW data. */
                    // // if (send_param->state == 0) {
                    // //     send_param->state = 1;
                    // // }

                //     /* If receive broadcast ESPNOW data which indicates that the other device has received
                //      * broadcast ESPNOW data and the local magic number is bigger than that in the received
                //      * broadcast ESPNOW data, stop sending broadcast ESPNOW data and start sending unicast
                //      * ESPNOW data.
                //      */
                //     if (recv_state == 1) {
                //         /* The device which has the bigger magic number sends ESPNOW data, the other one
                //          * receives ESPNOW data.
                //          */
                //         if (send_param->unicast == false && send_param->magic >= recv_magic) {
                //     	    ESP_LOGI(TAG, "Start sending unicast data");
                //     	    ESP_LOGI(TAG, "send data to "MACSTR"", MAC2STR(recv_cb->mac_addr));

                //     	    /* Start sending unicast ESPNOW data. */
                //             memcpy(send_param->dest_mac, recv_cb->mac_addr, ESP_NOW_ETH_ALEN);
                //             example_espnow_data_prepare(send_param);
                //             if (esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK) {
                //                 ESP_LOGE(TAG, "Send error");
                //                 example_espnow_deinit(send_param);
                //                 vTaskDelete(NULL);
                //             }
                //             else {
                //                 send_param->broadcast = false;
                //                 send_param->unicast = true;
                //             }
                //         }
                //     }
                // }
                //else 
                if (ret == EXAMPLE_ESPNOW_DATA_UNICAST) {
                    ESP_LOGI(TAG, "Receive %dth unicast data from: "MACSTR", len: %d", recv_seq, MAC2STR(recv_cb->mac_addr), recv_cb->data_len);
                    
                    snprintf(msg.message, sizeof(msg.message), "Received from "MACSTR": Data length %d",
                    MAC2STR(recv_cb->mac_addr), recv_cb->data_len);
                    lv_label_set_text(espnow_label, msg.message);

                    /* If receive unicast ESPNOW data, change param of ACK */
                    if (send_param->state == 0) {
                         send_param->state = 1;
                     }
                    //TODO: Determine if this is needed
                    send_param->broadcast = false;
                    send_param->unicast = true;
                }
                else {
                    ESP_LOGI(TAG, "Receive error data from: "MACSTR"", MAC2STR(recv_cb->mac_addr));

                    snprintf(msg.message, sizeof(msg.message), "Receive error data from: "MACSTR"",
                    MAC2STR(recv_cb->mac_addr));
                    lv_label_set_text(espnow_label, msg.message);
                }
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
    example_espnow_send_param_t *send_param;
    uint8_t own_mac[ESP_NOW_ETH_ALEN];
    ESP_ERROR_CHECK(esp_wifi_get_mac(ESP_IF_WIFI_STA, own_mac));  // Get self MAC address

    s_example_espnow_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(example_espnow_event_t));
    if (s_example_espnow_queue == NULL) {
        ESP_LOGE(TAG, "Create mutex fail");
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
    /* Set primary master key. */
    // ESP_ERROR_CHECK( esp_now_set_pmk((uint8_t *)CONFIG_ESPNOW_PMK) );

    /* Add broadcast peer information to peer list. */
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

    /* Initialize sending parameters. */
    send_param = malloc(sizeof(example_espnow_send_param_t));
    if (send_param == NULL) {
        ESP_LOGE(TAG, "Malloc send parameter fail");
        vSemaphoreDelete(s_example_espnow_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }
    memset(send_param, 0, sizeof(example_espnow_send_param_t));
    send_param->unicast = false;
    send_param->broadcast = true;
    send_param->state = 0;
    send_param->magic = esp_random();
    send_param->count = CONFIG_ESPNOW_SEND_COUNT;
    send_param->delay = CONFIG_ESPNOW_SEND_DELAY;
    send_param->len = CONFIG_ESPNOW_SEND_LEN;
    send_param->buffer = malloc(CONFIG_ESPNOW_SEND_LEN);
    if (send_param->buffer == NULL) {
        ESP_LOGE(TAG, "Malloc send buffer fail");
        free(send_param);
        vSemaphoreDelete(s_example_espnow_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }

    //TODO: Needs to be update to work for multiple robots
    for (int i = 0; i < NUM_ROBOTS; i++) {
        if (memcmp(mac_addresses[i], own_mac, ESP_NOW_ETH_ALEN) == 0) {
            continue;  // Skip adding own MAC
        }   
        memcpy(send_param->dest_mac, mac_addresses[i], ESP_NOW_ETH_ALEN);
    }
    example_espnow_data_prepare(send_param);

    xTaskCreate(example_espnow_task, "example_espnow_task", 3072, send_param, 4, NULL);

    return ESP_OK;
}

static void example_espnow_deinit(example_espnow_send_param_t *send_param)
{
    free(send_param->buffer);
    free(send_param);
    vSemaphoreDelete(s_example_espnow_queue);
    esp_now_deinit();
}
