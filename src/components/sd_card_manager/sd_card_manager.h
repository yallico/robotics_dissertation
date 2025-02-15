#ifndef SD_CARD_MANAGER_H
#define SD_CARD_MANAGER_H

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UPLOAD_COMPLETED_BIT BIT2
#define MAX_FILE_SIZE 1 * 1024 
extern SemaphoreHandle_t sd_card_mutex;

// Function to initialize the SD card
esp_err_t init_sd_card(const char* mount_point);

// Function to clean the SD card
void clean_sd_card(const char* mount_point);

// Function to write data to a file with size management
esp_err_t write_data(const char* base_path, const char* data, const char* suffix);
esp_err_t read_data(const char *path, char *buffer, size_t buffer_size, size_t *data_size);
void upload_all_sd_files();
void unmount_sd_card(const char* mount_point);
void test_sd_card();

#ifdef __cplusplus
}
#endif

#endif // SD_CARD_MANAGER_H
