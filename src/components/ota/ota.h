#ifndef OTA_H
#define OTA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "string.h"
#include "nvs.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "esp_event.h"

esp_err_t _http_event_handler(esp_http_client_event_t *evt);
void simple_ota_example_task(void);
void ota_task(void *pvParameter);
void print_sha256(const uint8_t *image_hash, const char *label);
void get_sha256_of_partitions(void);
bool is_new_version(const char* current_version, const char* new_version);
esp_err_t http_event_handler(esp_http_client_event_t *evt);
void ota_check_ver();


#ifdef __cplusplus
}
#endif
#endif
