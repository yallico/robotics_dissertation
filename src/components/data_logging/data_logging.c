#include "data_logging.h"
#include "esp_log.h"
#include <time.h>
#include "rtc_m5.h"
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include "sd_card_manager.h"
#include "data_structures.h"
#include "esp_app_desc.h"
#include "globals.h"

static const char *TAG = "LOG";

// This queue is used to send log messages, across multiple other tasks
extern QueueHandle_t LogQueue;
extern QueueHandle_t LogBodyQueue;
QueueSetHandle_t logSet = NULL;

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
    cJSON_AddStringToObject(root, "data_link", metadata->data_link);
    cJSON_AddStringToObject(root, "routing", metadata->routing);
    cJSON_AddNumberToObject(root, "msg_limit", metadata->msg_limit);
    cJSON_AddStringToObject(root, "com_type", metadata->com_type);
    cJSON_AddNumberToObject(root, "msg_size_bytes", metadata->msg_size_bytes);
    cJSON_AddNumberToObject(root, "pop_size", metadata->pop_size);
    cJSON_AddNumberToObject(root, "max_genes", metadata->max_genes);
    cJSON_AddNumberToObject(root, "robot_speed", metadata->robot_speed);
    cJSON_AddNumberToObject(root, "experiment_start", (long)(metadata->experiment_start));
    cJSON_AddNumberToObject(root, "experiment_end", (long)(metadata->experiment_end));
    cJSON_AddNumberToObject(root, "seed", metadata->seed);
    char app_version_str[8];
    snprintf(app_version_str, sizeof(app_version_str), "%.2f", metadata->app_version);
    cJSON_AddStringToObject(root, "app_version", app_version_str);
    cJSON_AddNumberToObject(root, "experiment_duration", metadata->experiment_duration);
    cJSON_AddStringToObject(root, "migration_type", metadata->migration_type);
    cJSON_AddNumberToObject(root, "topology", metadata->topology);
    cJSON_AddNumberToObject(root, "migration_rate", metadata->migration_rate);
    cJSON_AddNumberToObject(root, "patience", metadata->patience);
    cJSON_AddNumberToObject(root, "mass_extinction", metadata->mass_extinction);
    cJSON_AddNumberToObject(root, "hypermutation_generations", metadata->s_hyper_mutation_generations);
    
    char *json_data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);  

    return json_data;
}

char*  log_experiment_metadata(experiment_metadata_t *metadata) {

    strncpy(metadata->experiment_id, experiment_id, sizeof(metadata->experiment_id) - 1);
    strncpy(metadata->robot_id, robot_id, sizeof(metadata->robot_id) - 1);
    metadata->seed = seed;
    metadata->num_robots = DEFAULT_NUM_ROBOTS;
    metadata->data_link = DEFAULT_DATA_LINK;
    metadata->routing = DEFAULT_ROUTING;
    metadata->msg_limit = DEFAULT_MSG_LIMIT;
    metadata->com_type = DEFAULT_COM_TYPE;
    metadata->msg_size_bytes = sizeof(out_message_t);
    metadata->robot_speed = DEFAULT_ROBOT_SPEED;
    metadata->pop_size = POP_SIZE;
    metadata->max_genes = MAX_GENES;
    metadata->experiment_start = experiment_start;
    metadata->experiment_end = experiment_end;
    metadata->experiment_duration = DEFAULT_EXPERIMENT_DURATION;
    metadata->migration_type = DEFAULT_MIGRATION_TYPE;
    metadata->migration_scheme = DEFAULT_MIGRATION_SCHEME;
    metadata->topology = DEFAULT_TOPOLOGY;
    metadata->migration_rate = DEFAULT_MIGRATION_RATE;
    metadata->migration_frequency = DEFAULT_MIGRATION_FREQUENCY;
    metadata->patience = DEFAULT_PATIENCE;
    metadata->mass_extinction = DEFAULT_MASS_EXTINCTION;
    metadata->s_hyper_mutation_generations = DEFAULT_HYPERMUTATION_GENERATIONS;
    
    //app version
    const esp_app_desc_t *app_desc = esp_app_get_description();
    float app_version = atof(app_desc->version);  //convert to float
    metadata->app_version = app_version;

    char *json_data = serialize_metadata_to_json(metadata);
        if (json_data == NULL) {
            ESP_LOGE(TAG, "Failed to serialize JSON");
            return NULL;
        }
    
    return json_data;

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

    char *json_data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    return json_data;  // caller must free this string
}

char* serialize_log_body_to_json(const event_log_message_t *log_message) {
    cJSON *root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, "log_id", log_message->log_id);
    cJSON_AddNumberToObject(root, "log_datetime", (long)(log_message->log_datetime));
    cJSON_AddStringToObject(root, "log_message", log_message->log_message);

    char *json_data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    return json_data;  // caller must free this string
}

// task to write data
void write_task(void *pvParameters) {

    event_log_t log_entry;
    event_log_message_t log_body;

    for (;;) {
        /* Block here until SOMETHING arrives on EITHER queue */
        QueueSetMemberHandle_t h = xQueueSelectFromSet(logSet, portMAX_DELAY);

        if (h == LogQueue) {
            xQueueReceive(LogQueue, &log_entry, 0);   // 0-tick, we know it’s ready
            //serialize the log to json
            char* json_data = serialize_log_to_json(&log_entry); 
            // call the sd_card_manager to write the log in memory
            write_data("/sdcard", json_data, "log");

            free(json_data);

        } else if (h == LogBodyQueue) {
            xQueueReceive(LogBodyQueue, &log_body, 0);
            //serialize the log to json
            char* json_data = serialize_log_body_to_json(&log_body); 
            // call the sd_card_manager to write the log in memory
            write_data("/sdcard", json_data, "message");

            free(json_data);
            
        }
    }

}
