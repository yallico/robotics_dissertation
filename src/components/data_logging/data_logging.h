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

void write_task(void *pvParameters);
char* serialize_metadata_to_json(const experiment_metadata_t *metadata);
char* generate_experiment_id(RTC_DateTypeDef *date, RTC_TimeTypeDef *time);
time_t convert_to_time_t(RTC_DateTypeDef *date, RTC_TimeTypeDef *time);

#ifdef __cplusplus
}
#endif

#endif // DATA_LOGGING_H
