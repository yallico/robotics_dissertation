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

typedef struct {
    char *data;      // Pointer to store data
    int len;         // Length of data
} http_response_t;

// esp_err_t https_put(const char *url, const char *data, size_t data_len);
esp_err_t https_put_stream(const char *url, const char *path, size_t fsize);
esp_err_t https_get(const char *url, http_response_t *response, const uint8_t *cert);
esp_err_t test_https_cert_connection();

#ifdef __cplusplus
}
#endif

#endif // HTTPS_COMPONENT_H
