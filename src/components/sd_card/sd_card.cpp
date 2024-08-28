#include "Arduino.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "esp_log.h"  // Include ESP-IDF logging
#include "sd_card.h"
#include "freertos/semphr.h"

static const char* TAG = "SD_CARD";  // Define a tag for logging
SemaphoreHandle_t spiSemaphore = NULL;

// Initialize the SD card
void sd_card_init() {

    spiSemaphore = xSemaphoreCreateMutex();

    if (spiSemaphore == NULL) {
        ESP_LOGE(TAG, "Failed to create semaphore for SPI");
        return;  // Early return if semaphore creation fails
    }

    if (xSemaphoreTake(spiSemaphore, portMAX_DELAY) == pdTRUE) {
        SPIClass spi(HSPI);
        spi.begin(18, 38, 23, -1);

        if (!SD.begin(4, spi)) {
            ESP_LOGE(TAG, "Card Mount Failed");
            xSemaphoreGive(spiSemaphore);
            return;
        }
        uint8_t cardType = SD.cardType();

        if (cardType == CARD_NONE) {
            ESP_LOGE(TAG, "No SD card attached");
            xSemaphoreGive(spiSemaphore);
            return;
        }

        ESP_LOGI(TAG, "SD card initialized.");
        xSemaphoreGive(spiSemaphore);
    }
}

// Test writing to a file on the SD card
void sd_card_write_task(void *pvParameters) {
    if (xSemaphoreTake(spiSemaphore, portMAX_DELAY) == pdTRUE) {
        File file = SD.open("/test.txt", FILE_WRITE);
        if (!file) {
            ESP_LOGE(TAG, "Failed to open file for writing");
        } else {
            if (file.println("Hello, M5Stack!")) {
                ESP_LOGI(TAG, "File written");
            } else {
                ESP_LOGE(TAG, "Write failed");
            }
            file.close(); // Close the file
        }
        xSemaphoreGive(spiSemaphore);
    }
    vTaskDelete(NULL);
}
