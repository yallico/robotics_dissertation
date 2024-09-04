#ifndef _DATA_STRUCTURES_H
#define _DATA_STRUCTURES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h> 
#include <stdint.h>
#include <time.h>

#define MAX_ROBOTS 20  //maximum number of robots

typedef struct {
    char experiment_id[16];
    char robot_id[5];
    int num_robots;
    uint16_t robot_ids[MAX_ROBOTS];
    int seed;                // Seed used in the experiment
    char *data_link;        // Nullable string
    char *routing;          // Nullable string
    int msg_limit;  
    char *com_type;         // Nullable string
    int msg_size_bytes;
    int robot_speed;
    time_t experiment_start; // Timestamp for the start of the experiment
    time_t experiment_end;   // Timestamp for the end of the experiment
    float app_version;       // Application version number
} experiment_metadata_t;

typedef struct {
    uint32_t log_id;
    time_t log_datetime;
    char *status;
    char *tag;
    char *log_level;
    char *log_type;
    char *from_id[5];
} event_log_t;

typedef struct {
    uint32_t log_id;
    time_t log_datetime;
    char *sub_tag;
    char log_message[512];
} event_log_message_t;

extern experiment_metadata_t metadata;
extern event_log_t event_log;
extern event_log_message_t message;

#ifdef __cplusplus
}
#endif

#endif 
