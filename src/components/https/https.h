#ifndef HTTPS_COMPONENT_H
#define HTTPS_COMPONENT_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Upload data to a specified URL via HTTPS POST or PUT.
 * 
 * @param url The URL to which the data should be uploaded.
 * @param data Pointer to the data to upload.
 * @param data_len Length of the data to upload.
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t https_put(const char *url, const char *data, size_t data_len);

#ifdef __cplusplus
}
#endif

#endif // HTTPS_COMPONENT_H
