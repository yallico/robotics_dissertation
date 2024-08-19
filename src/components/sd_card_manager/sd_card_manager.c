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


#define MAX_FILE_SIZE 512000  // Max file size in bytes

// Initialize SD card
esp_err_t init_sd_card(const char* mount_point) {
    esp_err_t ret;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = VSPI_HOST; //HSPI_HOST is being used by lvgl
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = 23,  // Master Out Slave In
        .miso_io_num = 38,  // Master In Slave Out
        .sclk_io_num = 18,  // Serial Clock
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = 4;
    slot_config.host_id = host.slot;

    // Initialize the SPI bus
    ret = spi_bus_initialize(host.slot, &bus_cfg, 2);
    if (ret != ESP_OK) {
        ESP_LOGE("SPI", "Failed to initialize bus.");
        return ret;
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t* card;
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);

    return ret;
}

// Clean SD card
void clean_sd_card(const char* base_path) {
    DIR* dir = opendir(base_path);
    if (dir != NULL) {
        struct dirent* ent;
        char file_path[512];
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
                sprintf(file_path, "%s/%s", base_path, ent->d_name);
                unlink(file_path);  // Delete the file
            }
        }
        closedir(dir);
    }
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
