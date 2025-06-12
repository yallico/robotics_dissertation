#include "https.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "ota.h"

static const char *TAG = "HTTPS_COMPONENT";
#define TEST_URL "https://robotics-dissertation.s3.eu-north-1.amazonaws.com/"

// HTTP event handler
static esp_err_t http_handler_metadata(esp_http_client_event_t *evt) {
    http_response_t *response = (http_response_t*) evt->user_data;  // Passed as user data

    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_DATA:
            // Allocate or extend the buffer on every chunk received
            if (response->data == NULL) {  // First chunk of data
                response->data = malloc(evt->data_len + 1);  // Allocate memory for data
                if (response->data == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate memory for response data.");
                    return ESP_ERR_NO_MEM;
                }
                response->len = 0; // Initialize length if first chunk
            } else {  // Subsequent data chunks
                char *new_data = realloc(response->data, response->len + evt->data_len + 1);
                if (new_data == NULL) {
                    ESP_LOGE(TAG, "Failed to reallocate memory for response data.");
                    free(response->data); // Free the original data to avoid memory leak
                    response->data = NULL; // Avoid dangling pointer
                    return ESP_ERR_NO_MEM;  // Handle realloc failure
                }
                response->data = new_data;
            }
            memcpy(response->data + response->len, evt->data, evt->data_len);
            response->len += evt->data_len;
            response->data[response->len] = '\0';  // Null-terminate the string
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t https_get(const char *url, http_response_t *response, const uint8_t *cert) {
    esp_http_client_config_t config = {
        .url = url,
        .cert_pem = (char *)cert,
        .event_handler = http_handler_metadata,
        .user_data = response  // Pass the response structure to the handler
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTPS GET Status = %d, content_length = %lld",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTPS GET request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

esp_err_t https_put(const char *url, const char *data, size_t data_len) {
    esp_http_client_config_t config = {
        .url = url,
        .cert_pem = (char *)server_cert_pem_start,
        .timeout_ms = 9000,
        .event_handler = http_handler_metadata,
    };

    //ESP_LOGI(TAG, "Using cert_pem at address: %p", server_cert_pem_start);
    ESP_LOGI(TAG, "Uploading %d bytes", (int)data_len);

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_url(client, url);
    esp_http_client_set_method(client, HTTP_METHOD_PUT);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    if (esp_http_client_open(client, data_len) != ESP_OK) {
        ESP_LOGE(TAG, "open() failed");
        esp_http_client_cleanup(client);
        vTaskDelay(20 / portTICK_PERIOD_MS);
        return ESP_FAIL;
    }

    // Write data in a loop to handle partial writes
    size_t total_written = 0;
    int written = 0; 

    while (total_written < data_len) {
        written = esp_http_client_write(client, data + total_written, data_len - total_written);
        if (written < 0) {
            ESP_LOGE(TAG, "HTTP write failed: %d", written);
            break;
        }
        total_written += written;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTPS PUT Status = %d, content_length = %lld",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTPS PUT request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    vTaskDelay(20 / portTICK_PERIOD_MS);
    return err;
}

esp_err_t test_https_cert_connection() {
    ESP_LOGI(TAG, "Testing HTTPS connection to %s", TEST_URL);

    esp_http_client_config_t config = {
        .url = TEST_URL,
        .cert_pem = (char *)server_cert_pem_start,
        .timeout_ms = 9000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTPS test passed, status code: %d", status_code);
    } else {
        ESP_LOGE(TAG, "HTTPS test failed, error: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}