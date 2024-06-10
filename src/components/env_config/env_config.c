#include <stdint.h>
#include "esp_idf_version.h"

// Get the current ESP-IDF version
uint32_t current_esp_version(void){
    return ESP_IDF_VERSION;
}

// Expected ESP-IDF version: major, minor, and patch
uint32_t expected_esp_version(void){
    return ESP_IDF_VERSION_VAL(5, 4, 0);
}