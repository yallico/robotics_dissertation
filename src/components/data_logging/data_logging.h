// data_logging.h

#ifndef DATA_LOGGING_H
#define DATA_LOGGING_H

#include "cJSON.h"
#include "data_structures.h"
#include "rtc_m5.h"
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_Q_LEN       20
#define LOG_BODY_Q_LEN  20

extern QueueSetHandle_t logSet;

void write_task(void *pvParameters);
char* generate_experiment_id(RTC_DateTypeDef *date, RTC_TimeTypeDef *time);
char* log_experiment_metadata(experiment_metadata_t *metadata);
time_t convert_to_time_t(RTC_DateTypeDef *date, RTC_TimeTypeDef *time);

#ifdef __cplusplus
}
#endif

#endif // DATA_LOGGING_H
