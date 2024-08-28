#ifndef SD_CARD_H
#define SD_CARD_H

#include "freertos/semphr.h" 

#ifdef __cplusplus
extern "C" {
#endif

extern SemaphoreHandle_t spiSemaphore;

// Function declarations
void sd_card_init();       // Initialize the SD card
void sd_card_write_task(void *pvParameters); // Test writing to the SD card

#ifdef __cplusplus
}
#endif

#endif // SD_CARD_H
