#ifndef _DATA_STRUCTURES_H
#define _DATA_STRUCTURES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h> 
#include <stdint.h>
#include <time.h>

typedef struct {
    uint32_t log_id;
    char robot_id[5];
    time_t created_datetime;
    char message[64];
} out_message_t;

typedef struct {
    char experiment_id[16];  // Experiment ID, yyyymmddhhmmss format
    char robot_id[5];        // Robot ID, typically the last 4 digits of MAC address
    int num_robots;         // Number of robots in the experiment
    int seed;                // Seed used in the experiment
    char *data_link;        // Nullable string
    char *routing;          // Nullable string
    int msg_limit;          // Message capacity for the experiment
    char *com_type;         // Nullable string
    int msg_size_bytes;     // Size of the message in bytes
    int robot_speed;        // speed used in Pololu
    int pop_size;            // ga population size
    int max_genes;           // ga max genes
    time_t experiment_start; // Timestamp for the start of the experiment
    time_t experiment_end;   // Timestamp for the end of the experiment
    int experiment_duration; // Duration in seconds
    char *migration_type;    // e.g., "ASYNC"
    char *migration_scheme;  // e.g. "ELITIST"
    char *topology;          // e.g., "FC"
    int migration_rate;
    int migration_frequency; // seconds, 0 for patience based
    int patience;
    int mass_extinction;
    int s_hyper_mutation_generations;
    float app_version;       // Application version number
} experiment_metadata_t;

typedef struct {
    uint32_t log_id;
    time_t log_datetime;
    char status[2];
    char tag[2];
    char log_level[2];
    char log_type[128];
    char from_id[18];
} event_log_t;

typedef struct {
    uint32_t log_id;
    time_t log_datetime;
    char log_message[512];
} event_log_message_t;

extern experiment_metadata_t metadata;
extern event_log_t event_log;
extern event_log_message_t message;

#ifdef __cplusplus
}
#endif

#endif
