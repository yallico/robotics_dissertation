#ifndef GLOBALS_H
#define GLOBALS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "rtc_m5.h"
#include "freertos/semphr.h"
#include "time.h"

//RTC
extern RTC_TimeTypeDef global_time;
extern RTC_DateTypeDef global_date;
extern volatile bool experiment_started;
extern volatile bool experiment_ended;
extern uint32_t experiment_start_ticks;

//Logging
extern uint32_t log_counter;
extern QueueHandle_t LogQueue;
extern QueueHandle_t LogBodyQueue;
extern SemaphoreHandle_t logCounterMutex;
extern QueueHandle_t ga_buffer_queue;

//Genetic Algorithm
#define POP_SIZE        60      // How many candidate solutions to evaluate.
                                // More = better search, but slower to compute
                                // Less = harder for algorithm to find solution
                                
#define MAX_GENES       10       // Dimensions (higher = more difficult)
                                // This is the number of parameters to be 
                                // represented in the "genotype" (candidate
                                // solution).

#define DEFAULT_MIGRATION_RATE 1 // Number of genome migrated from remote robot
                                // to local population per generation

#define DEFAULT_GENE_OVERWRITE 0.05f // Percentage of population to overwrite with remote genes

#define DEFAULT_PATIENCE 60

#define DEFAULT_MASS_EXTINCTION POP_SIZE/2

#define DEFAULT_HYPERMUTATION_GENERATIONS 20

#define DEFAULT_NUM_ROBOTS 3

#define MSG_UNLIMITED     0
#define MSG_LIMITED       1
#define DEFAULT_MSG_LIMIT MSG_LIMITED

#define DEFAULT_EXPERIMENT_DURATION 60 //seconds

#define DEFAULT_ROBOT_SPEED 0.0f

#define DEFAULT_DATA_LINK "ESPNOW"

#define DEFAULT_ROUTING "UNICAST"

#define DEFAULT_COM_TYPE "DIRECT"

#define DEFAULT_MIGRATION_TYPE "ASYNC"

#define DEFAULT_MIGRATION_SCHEME "ELITIST"

#define FREQUENCY_NONE      0
#define FREQUENCY_RANDOM    1
#define DEFAULT_MIGRATION_FREQUENCY FREQUENCY_NONE

#define TOPOLOGY_RANDOM      0
#define TOPOLOGY_COMM_AWARE  1
#define DEFAULT_TOPOLOGY TOPOLOGY_COMM_AWARE

//Experimental metadata
extern char* experiment_id;
extern char* robot_id;
extern time_t experiment_start;
extern time_t experiment_end;
extern uint16_t seed;
extern int experiment_duration; // Duration in seconds
extern char* migration_type;    // e.g., "ASYNC"
extern char* migration_scheme;  // e.g. "ELITIST"
extern int topology;          // e.g., "RANDOM"/"COMM_AWARE"
extern int migration_rate;      
extern int migration_frequency; //seconds, 0 for patience based
extern int patience;
extern int mass_extinction;
extern int s_hyper_mutation_generations;

//SD Card
extern const char* mount_point;


#ifdef __cplusplus
}
#endif

#endif // GLOBALS_H
