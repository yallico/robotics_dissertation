#include "sd_card_manager.h"
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
//#include "driver/sdmmc_host.h"
#include <unistd.h>


#define MAX_FILE_SIZE 64  // Max file size in bytes
static const char *TAG = "SD_CARD_MANAGER";

// Initialize SD card
esp_err_t init_sd_card(const char* mount_point) {
    esp_err_t ret;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = HSPI_HOST; //HSPI_HOST is being used by lvgl
    //host.flags = SDMMC_HOST_FLAG_4BIT;  // Force 4-line mode
    //host.max_freq_khz = SDMMC_FREQ_PROBING; // 4MHz

    // spi_bus_config_t bus_cfg = {
    //     .mosi_io_num = 23,  // Master Out Slave In
    //     .miso_io_num = 38,  // Master In Slave Out
    //     .sclk_io_num = 18,  // Serial Clock
    //     .quadwp_io_num = -1,
    //     .quadhd_io_num = -1,
    //     .max_transfer_sz = 4000,
    // };

    // Initialize the SPI bus
    // ret = spi_bus_initialize(host.slot, &bus_cfg, 2);
    // if (ret != ESP_OK) {
    //     ESP_LOGE(TAG, "Failed to initialize bus.");
    //     return ret;
    // }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = 4;
    slot_config.host_id = host.slot;


    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t* card;
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);

    return ret;
}

void clean_sd_card(const char* base_path) {
    DIR* dir = opendir(base_path);
    if (dir == NULL) {
        ESP_LOGE("SD_CARD", "Failed to open directory: %s", base_path);
        return;
    }

    struct dirent* ent;
    char file_path[512];
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
            snprintf(file_path, sizeof(file_path), "%s/%s", base_path, ent->d_name);

            // Check if it is a regular file and not a directory
            struct stat path_stat;
            stat(file_path, &path_stat);
            if (S_ISREG(path_stat.st_mode)) {
                if (unlink(file_path) == -1) {
                    ESP_LOGE("SD_CARD", "Failed to delete file: %s", file_path);
                } else {
                    ESP_LOGI("SD_CARD", "Deleted file: %s", file_path);
                }
            }
        }
    }
    closedir(dir);
}

esp_err_t write_data(const char* base_path, const char* data, const char* suffix) {
    static int file_index = 0;
    static char file_name[256];
    struct stat st;

    while (1) {
        // Update the filename to include the suffix and file index
        sprintf(file_name, "%s/%s_%d.json", base_path, suffix, file_index);

        // Check if the file exists and its size
        if (stat(file_name, &st) == 0) {
            if (st.st_size > MAX_FILE_SIZE) {
                // If the current file is too large, move to the next file index
                file_index++;
                continue; // Skip the rest of the loop and check the next file
            }
        } else {
            // If the file does not exist, it will be created by fopen
            break;
        }

        break; // If the file exists and is not too large, break the loop to write to it
    }

    // Open the file for appending
    FILE* f = fopen(file_name, "a");
    if (f == NULL) {
        return ESP_FAIL;
    }

    // Write the data to the file
    fprintf(f, "%s\n", data);
    fclose(f);
    return ESP_OK;
}

esp_err_t s_example_write_file(const char *path, char *data) {
    ESP_LOGI(TAG, "Opening file %s", path);
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return ESP_FAIL;
    }
    fprintf(f, data);
    fclose(f);
    ESP_LOGI(TAG, "File written");

    return ESP_OK;
}

esp_err_t s_example_read_file(const char *path) {
    ESP_LOGI(TAG, "Reading file %s", path);
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return ESP_FAIL;
    }
    char line[MAX_FILE_SIZE];
    fgets(line, sizeof(line), f);
    fclose(f);

    // strip newline
    char *pos = strchr(line, '\n');
    if (pos) {
        *pos = '\0';
    }
    ESP_LOGI(TAG, "Read from file: '%s'", line);

    return ESP_OK;
}

void test_sd_card() {
    FILE* f = fopen("/sdcard/test.txt", "w+");
    if (f == NULL) {
        ESP_LOGE("SD_CARD", "Failed to open file for writing");
        return;
    }
    fprintf(f, "Hello, World!");
    fclose(f);

    f = fopen("/sdcard/test.txt", "r");
    if (f == NULL) {
        ESP_LOGE("SD_CARD", "Failed to open file for reading");
        return;
    }
    char line[100];
    fgets(line, sizeof(line), f);
    fclose(f);

    ESP_LOGI("SD_CARD", "Read from file: '%s'", line);
}

// Function to unmount the SD card
void unmount_sd_card(const char* mount_point) {
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(mount_point, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE("SD_CARD", "Failed to unmount SD card: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI("SD_CARD", "SD card unmounted successfully");
    }
}