#include "https.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "ota.h"

static const char *TAG = "HTTPS_COMPONENT";
#define TEST_URL "https://robotics-dissertation.s3.eu-north-1.amazonaws.com/"
#define CHUNK  4096

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

esp_err_t https_put_stream(const char *url, const char *path, size_t fsize)
{
    esp_http_client_config_t cfg = {
        .url          = url,
        .method       = HTTP_METHOD_PUT,
        .cert_pem     = (char *)server_cert_pem_start,
        .timeout_ms   = 20000,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) return ESP_FAIL;

    ESP_ERROR_CHECK( esp_http_client_set_header(cli,
                          "Content-Type", "application/json") );

    /* Open the connection and tell S3 how many bytes will follow */
    esp_err_t open_err = esp_http_client_open(cli, fsize);
    if (open_err != ESP_OK) {
        ESP_LOGE(TAG, "PUT stream failed: %s", esp_err_to_name(open_err));
        esp_http_client_cleanup(cli);
        return open_err;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) { esp_http_client_cleanup(cli); return ESP_FAIL; }

    static uint8_t buf[CHUNK];
    size_t nread;

    while ((nread = fread(buf, 1, CHUNK, fp)) > 0) {

        size_t sent = 0;
        while (sent < nread) {          // ← keep writing until this chunk is gone
            int wr = esp_http_client_write(cli,
                                           (char *)buf + sent,
                                           nread - sent);
            if (wr < 0) {               // fatal TLS error
                ESP_LOGE(TAG, "TLS write failed (%d)", wr);
                fclose(fp);
                esp_http_client_cleanup(cli);
                return ESP_FAIL;
            }
            if (wr == 0) {              // nothing accepted ⇒ timeout
                ESP_LOGE(TAG, "TLS write timeout");
                fclose(fp);
                esp_http_client_cleanup(cli);
                return ESP_FAIL;
            }
            sent += wr;                 // advance pointer
            
            // ESP_LOGD(TAG, "wrote %d / %d B", wr, nread);
        }
    }

    fclose(fp);

    /* Complete the request and fetch the response */
    esp_err_t err = esp_http_client_perform(cli);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "PUT %s → %d",
                 url, esp_http_client_get_status_code(cli));
    } else {
        ESP_LOGE(TAG, "HTTPS PUT failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(cli);
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