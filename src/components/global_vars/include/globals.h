#ifndef GLOBALS_H
#define GLOBALS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "rtc_m5.h"

//RTC
extern RTC_TimeTypeDef global_time;
extern RTC_DateTypeDef global_date;
extern volatile bool experiment_started;
extern volatile bool experiment_ended;
extern uint32_t experiment_start_ticks;

#ifdef __cplusplus
}
#endif

#endif // GLOBALS_H
