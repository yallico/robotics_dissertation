#include "sd_card_manager.h"
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include <unistd.h>
#include "globals.h"
#include "https.h"


static const char *TAG = "SD_CARD_MANAGER";
sdmmc_card_t* card;
static int file_index = 0;

SemaphoreHandle_t sd_card_mutex = NULL;

// Initialize SD card
esp_err_t init_sd_card(const char* mount_point) {
    esp_err_t ret;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = HSPI_HOST; //HSPI_HOST is being used by lvgl
    //host.flags = SDMMC_HOST_FLAG_4BIT;  // Force 4-line mode
    //host.max_freq_khz = SDMMC_FREQ_PROBING; // 4MHz

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = 4;
    slot_config.host_id = host.slot;


    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 3,
        .allocation_unit_size = 16 * 1024
    };

    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);

    //print sd card properties to confirm format
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
    static char file_name[256];
    struct stat st;

    while (1) {
        // Update the filename to include the suffix and file index
        sprintf(file_name, "%s/%s_%s_%d.json", base_path, experiment_id, suffix, file_index);

        // Check if the file exists and its size
        if (stat(file_name, &st) == 0) {
            ESP_LOGI(TAG, "File exists. Size: %ld", (long) st.st_size);
            if (st.st_size > MAX_FILE_SIZE) {
                // If the current file is too large, move to the next file index
                ESP_LOGI(TAG, "File is too large, incrementing file index.");
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
        ESP_LOGE(TAG, "Failed to open file for writing");
        return ESP_FAIL;
    }

    // Write the data to the file
    fprintf(f, "%s\n", data);
    fclose(f);
    return ESP_OK;
}

esp_err_t read_data(const char *path, char *buffer, size_t buffer_size, size_t *data_size) {
    ESP_LOGI(TAG, "Reading file %s", path);

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return ESP_FAIL;
    }

    //read into the buffer
    *data_size = fread(buffer, 1, buffer_size, f);
    if (*data_size == 0) {
        ESP_LOGE(TAG, "File is empty or failed to read: %s", path);
        fclose(f);
        return ESP_FAIL;
    }
    fclose(f);
    //ESP_LOGI(TAG, "Read %zu bytes from file: %s", *data_size, path);

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

void upload_all_sd_files() {
    // Open the SD card directory.
    DIR *dir = opendir(mount_point);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory: %s", mount_point);
        return;
    }

    // Allocate a buffer on the heap.
    char *file_buffer = (char *)malloc(MAX_FILE_SIZE);
    if (file_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for file buffer");
        closedir(dir);
        return;
    }

    size_t file_size;
    struct dirent *entry;

    // Loop through each file in the directory.
    while ((entry = readdir(dir)) != NULL) {
        ESP_LOGI(TAG, "Processing file: %s", entry->d_name);
        if (entry->d_type != DT_REG) {
            ESP_LOGW(TAG, "Skipping non-regular file: %s", entry->d_name);
            continue;
        }

        // Build the file path.
        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s/%s", mount_point, entry->d_name);
        ESP_LOGI(TAG, "Reading file: %s", filepath);

        // Read file data into the buffer.
        esp_err_t read_err = read_data(filepath, file_buffer, MAX_FILE_SIZE, &file_size);
        if (read_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read file: %s", filepath);
            continue;
        }


        const char *folder = NULL;
        if (strcasestr(entry->d_name, "log") != NULL) {
            folder = "logs";
        } else if (strcasestr(entry->d_name, "metadata") != NULL) {
            folder = "metadata";
        } else if (strcasestr(entry->d_name, "message") != NULL) {
            folder = "messages";
        }

        if (folder == NULL) {
            ESP_LOGW(TAG, "File name %s did not match any expected folder, skipping upload", entry->d_name);
            continue; 
        }

        // Build the upload URL.
        char presigned_url[512];
        snprintf(presigned_url, sizeof(presigned_url),
                 "https://robotics-dissertation.s3.eu-north-1.amazonaws.com/%s/%s/%s",
                 robot_id,
                 folder,
                 entry->d_name);
        ESP_LOGI(TAG, "Upload URL: %s", presigned_url);
        ESP_LOGI(TAG, "Largest free block: %u", heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));

        // Attempt the upload via HTTPS POST/PUT.
        esp_err_t upload_err = https_put(presigned_url, file_buffer, file_size);
        if (upload_err == ESP_OK) {
            ESP_LOGI(TAG, "Successfully uploaded file: %s", filepath);
            remove(filepath);  // Delete the file upon successful upload.
        } else {
            ESP_LOGE(TAG, "Failed to upload file: %s", filepath);
        }
    }

    // Cleanup.
    free(file_buffer);
    closedir(dir);
}


void unmount_sd_card(const char* mount_point) {
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(mount_point, card);
    if (ret != ESP_OK) {
        ESP_LOGE("SD_CARD", "Failed to unmount SD card: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI("SD_CARD", "SD card unmounted successfully");
    }
}