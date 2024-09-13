#include "data_logging.h"
#include "esp_log.h"
#include <time.h>
#include "rtc_m5.h"
#include <stdarg.h>
#include "sd_card_manager.h"
#include "data_structures.h"

// This queue is used to send log messages, across multiple other tasks
extern QueueHandle_t LogQueue;
extern QueueHandle_t LogBodyQueue;

char* generate_experiment_id(RTC_DateTypeDef *date ,RTC_TimeTypeDef *time) {
    static char id[16]; // Buffer to hold the formatted experiment ID

    snprintf(id, sizeof(id), "%02d%02d%02d%02d%02d",
             (date->Year % 100), // Last two digits of the year
             date->Month,
             date->Date,
             time->Hours,
             time->Minutes);

    return id;
}

time_t convert_to_time_t(RTC_DateTypeDef *date, RTC_TimeTypeDef *time) {
    struct tm t;

    t.tm_year = date->Year - 1900;  // tm_year is years since 1900
    t.tm_mon  = date->Month - 1;    // tm_mon is 0-based
    t.tm_mday = date->Date;
    t.tm_hour = time->Hours;
    t.tm_min  = time->Minutes;
    t.tm_sec  = time->Seconds;

    t.tm_isdst = -1;  // Negative for daylight savings information not available

    // Convert to time_t
    time_t time_stamp = mktime(&t);

    return time_stamp;
}

char* serialize_metadata_to_json(const experiment_metadata_t *metadata) {
    cJSON *root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "experiment_id", metadata->experiment_id);
    cJSON_AddStringToObject(root, "robot_id", metadata->robot_id);
    cJSON_AddNumberToObject(root, "num_robots", metadata->num_robots);
    cJSON *ids_array = cJSON_AddArrayToObject(root, "robot_ids");
    for (int i = 0; i < metadata->num_robots; i++) {
        cJSON_AddItemToArray(ids_array, cJSON_CreateNumber(metadata->robot_ids[i]));
    }
    cJSON_AddStringToObject(root, "data_link", metadata->data_link);
    cJSON_AddStringToObject(root, "routing", metadata->routing);
    cJSON_AddNumberToObject(root, "msg_limit", metadata->msg_limit);
    cJSON_AddStringToObject(root, "com_type", metadata->com_type);
    cJSON_AddNumberToObject(root, "msg_size_bytes", metadata->msg_size_bytes);
    cJSON_AddNumberToObject(root, "robot_speed", metadata->robot_speed);
    cJSON_AddNumberToObject(root, "experiment_start", (long)(metadata->experiment_start));
    cJSON_AddNumberToObject(root, "experiment_end", (long)(metadata->experiment_end));
    cJSON_AddNumberToObject(root, "seed", metadata->seed);
    cJSON_AddNumberToObject(root, "app_version", metadata->app_version);

    char *json_data = cJSON_Print(root);
    cJSON_Delete(root);  

    return json_data;  // caller must free this string
}

char* serialize_log_to_json(const event_log_t *log) {
    cJSON *root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, "log_id", log->log_id);
    cJSON_AddNumberToObject(root, "log_datetime", (long)(log->log_datetime));
    cJSON_AddStringToObject(root, "status", log->status);
    cJSON_AddStringToObject(root, "tag", log->tag);
    cJSON_AddStringToObject(root, "log_level", log->log_level);
    cJSON_AddStringToObject(root, "log_type", log->log_type);
    cJSON_AddStringToObject(root, "from_id", log->from_id);

    char *json_data = cJSON_Print(root);
    cJSON_Delete(root);

    return json_data;  // caller must free this string
}

char* serialize_log_body_to_json(const event_log_message_t *log_message) {
    cJSON *root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, "log_id", log_message->log_id);
    cJSON_AddNumberToObject(root, "log_datetime", (long)(log_message->log_datetime));
    cJSON_AddStringToObject(root, "log_message", log_message->log_message);

    char *json_data = cJSON_Print(root);
    cJSON_Delete(root);

    return json_data;  // caller must free this string
}

// task to write data
void write_task(void *pvParameters) {
    event_log_t log_entry;
    event_log_message_t log_body;

    while (1) {

        if (xQueueReceive(LogQueue, &log_entry, portMAX_DELAY) == pdTRUE) {
            //serialize the log to json
            char* json_data = serialize_log_to_json(&log_entry); 
            // call the sd_card_manager to write the log in memory
            write_data("/sdcard", json_data, "log");

            // free dynamically allocated fields
            free(log_entry.status);
            free(log_entry.tag);
            free(log_entry.log_level);
            free(log_entry.log_type);
            free(log_entry.from_id);

            free(json_data);
        }

        // Check log body queue
        if (xQueueReceive(LogBodyQueue, &log_body, portMAX_DELAY) == pdTRUE) {
            //serialize the log to json
            char* json_data = serialize_log_body_to_json(&log_body); 
            // call the sd_card_manager to write the log in memory
            write_data("/sdcard", json_data, "message");

            free(json_data);
        }

        vTaskDelay(10); //prevent task hogging CPU
    }
}
