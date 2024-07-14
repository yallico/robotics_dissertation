#ifndef RTC_M5_H
#define RTC_M5_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <time.h>
#include "i2c_manager.h"

// Define your I2C port and RTC address
#define RTC_I2C_PORT (i2c_port_t)CONFIG_M5CORE2_I2C_INTERNAL
#define RTC_ADDRESS 0x51

typedef struct {
    uint8_t Seconds;
    uint8_t Minutes;
    uint8_t Hours;
} RTC_TimeTypeDef;

typedef struct {
    uint8_t Date;
    uint8_t WeekDay;
    uint8_t Month;
    uint16_t Year;
} RTC_DateTypeDef;

esp_err_t rtc_m5_init(void);
void RTC_WriteReg(uint8_t reg, uint8_t data);
uint8_t RTC_ReadReg(uint8_t reg);
bool RTC_GetVoltLow(void);
void RTC_GetBm8563Time(void);
void RTC_Str2Time(void);
void RTC_DataMask(void);
void RTC_Bcd2asc(void);
uint8_t RTC_Bcd2ToByte(uint8_t Value);
uint8_t RTC_ByteToBcd2(uint8_t Value);
void RTC_GetTime(RTC_TimeTypeDef *RTC_TimeStruct);
int RTC_SetTime(RTC_TimeTypeDef *RTC_TimeStruct);
void RTC_GetDate(RTC_DateTypeDef *RTC_DateStruct);
int RTC_SetDate(RTC_DateTypeDef *RTC_DateStruct);
int RTC_SetAlarmIRQ_Seconds(int afterSeconds);
int RTC_SetAlarmIRQ_Time(const RTC_TimeTypeDef *RTC_TimeStruct);
int RTC_SetAlarmIRQ_DateTime(const RTC_DateTypeDef *RTC_DateStruct, const RTC_TimeTypeDef *RTC_TimeStruct);
void RTC_ClearIRQ(void);
void RTC_DisableIRQ(void);

void initialize_sntp(void);
void obtain_time(void);
void sync_rtc_with_ntp(void);

#ifdef __cplusplus
}
#endif
#endif
