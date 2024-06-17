#ifndef _ENV_CONFIG_H
#define _ENV_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "esp_idf_version.h"

void wifi_init_sta(void);
uint32_t current_esp_version(void);
uint32_t expected_esp_version(void);

#ifdef __cplusplus
}
#endif
#endif
