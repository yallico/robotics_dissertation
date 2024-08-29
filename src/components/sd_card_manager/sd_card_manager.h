#ifndef SD_CARD_MANAGER_H
#define SD_CARD_MANAGER_H

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

// Function to initialize the SD card
esp_err_t init_sd_card(const char* mount_point);

// Function to clean the SD card
void clean_sd_card(const char* mount_point);

// Function to write data to a file with size management
esp_err_t write_data(const char* base_path, const char* data, const char* suffix);
esp_err_t s_example_write_file(const char *path, char *data);
esp_err_t s_example_read_file(const char *path);
void unmount_sd_card(const char* mount_point);
void test_sd_card();

#ifdef __cplusplus
}
#endif

#endif // SD_CARD_MANAGER_H
