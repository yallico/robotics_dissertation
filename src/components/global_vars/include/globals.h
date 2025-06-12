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
#define POP_SIZE        20      // How many candidate solutions to evaluate.
                                // More = better search, but slower to compute
                                // Less = harder for algorithm to find solution
                                
#define MAX_GENES       5       // Dimensions (higher = more difficult)
                                // This is the number of parameters to be 
                                // represented in the "genotype" (candidate
                                // solution).

//Experimental metadata
extern char* experiment_id;
extern char* robot_id;
extern time_t experiment_start;
extern time_t experiment_end;
extern uint16_t seed;
#define DEFAULT_NUM_ROBOTS 1
#define DEFAULT_MSG_LIMIT 0
#define DEFAULT_MSG_SIZE_BYTES 32
#define DEFAULT_ROBOT_SPEED 0
#define DEFAULT_DATA_LINK "ESPNOW"
#define DEFAULT_ROUTING "UNICAST"
#define DEFAULT_COM_TYPE "DIRECT"

//SD Card
extern const char* mount_point;


#ifdef __cplusplus
}
#endif

#endif // GLOBALS_H
