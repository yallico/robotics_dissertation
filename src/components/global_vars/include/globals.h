#ifndef GLOBALS_H
#define GLOBALS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "rtc_m5.h"
#include "freertos/semphr.h"

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

#ifdef __cplusplus
}
#endif

#endif // GLOBALS_H
