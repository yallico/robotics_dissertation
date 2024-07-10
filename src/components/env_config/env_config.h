#ifndef _ENV_CONFIG_H
#define _ENV_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_idf_version.h"

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

extern EventGroupHandle_t s_wifi_event_group;
void wifi_init_sta(void);
uint32_t current_esp_version(void);
uint32_t expected_esp_version(void);

#ifdef __cplusplus
}
#endif
#endif
