#include "rtc_m5.h"
#include "esp_log.h"
#include "lwip/apps/sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "RTC";
static uint8_t trdata[7];
static uint8_t asc[14];
static uint8_t Second;
static uint8_t Minute;
static uint8_t Hour;

esp_err_t rtc_m5_init(void) {
    // Initialize I2C in your preferred way, if necessary
    RTC_WriteReg(0x00, 0x00);
    RTC_WriteReg(0x01, 0x00);
    RTC_WriteReg(0x0D, 0x00);

    return ESP_OK;
}

void RTC_WriteReg(uint8_t reg, uint8_t data) {
    uint8_t buffer[1] = { data };
    i2c_manager_write(RTC_I2C_PORT, RTC_ADDRESS, reg, buffer, 1);
}

uint8_t RTC_ReadReg(uint8_t reg) {
    uint8_t buffer[1];
    i2c_manager_read(RTC_I2C_PORT, RTC_ADDRESS, reg, buffer, 1);
    return buffer[0];
}

bool RTC_GetVoltLow(void) {
    return (RTC_ReadReg(0x02) & 0x80) >> 7;  // RTCC_VLSEC_MASK
}

void RTC_GetBm8563Time(void) {
    uint8_t buffer[7];
    i2c_manager_read(RTC_I2C_PORT, RTC_ADDRESS, 0x02, buffer, 7);
    for (int i = 0; i < 7; i++) {
        trdata[i] = buffer[i];
    }

    RTC_DataMask();
    RTC_Bcd2asc();
    RTC_Str2Time();
}

void RTC_Str2Time(void) {
    Second = (asc[0] - 0x30) * 10 + asc[1] - 0x30;
    Minute = (asc[2] - 0x30) * 10 + asc[3] - 0x30;
    Hour   = (asc[4] - 0x30) * 10 + asc[5] - 0x30;
}

void RTC_DataMask(void) {
    trdata[0] = trdata[0] & 0x7f;
    trdata[1] = trdata[1] & 0x7f;
    trdata[2] = trdata[2] & 0x3f;
    trdata[3] = trdata[3] & 0x3f;
    trdata[4] = trdata[4] & 0x07;
    trdata[5] = trdata[5] & 0x1f;
    trdata[6] = trdata[6] & 0xff;
}

void RTC_Bcd2asc(void) {
    uint8_t i, j;
    for (j = 0, i = 0; i < 7; i++) {
        asc[j++] = (trdata[i] & 0xf0) >> 4 | 0x30;
        asc[j++] = (trdata[i] & 0x0f) | 0x30;
    }
}

uint8_t RTC_Bcd2ToByte(uint8_t Value) {
    uint8_t tmp = 0;
    tmp = ((uint8_t)(Value & (uint8_t)0xF0) >> (uint8_t)0x4) * 10;
    return (tmp + (Value & (uint8_t)0x0F));
}

uint8_t RTC_ByteToBcd2(uint8_t Value) {
    uint8_t bcdhigh = Value / 10;
    return (bcdhigh << 4) | (Value - (bcdhigh * 10));
}

void RTC_GetTime(RTC_TimeTypeDef *RTC_TimeStruct) {
    uint8_t buf[3];
    i2c_manager_read(RTC_I2C_PORT, RTC_ADDRESS, 0x02, buf, 3);

    RTC_TimeStruct->Seconds = RTC_Bcd2ToByte(buf[0] & 0x7f);
    RTC_TimeStruct->Minutes = RTC_Bcd2ToByte(buf[1] & 0x7f);
    RTC_TimeStruct->Hours   = RTC_Bcd2ToByte(buf[2] & 0x3f);
}

int RTC_SetTime(RTC_TimeTypeDef *RTC_TimeStruct) {
    if (RTC_TimeStruct == NULL || RTC_TimeStruct->Hours > 24 ||
        RTC_TimeStruct->Minutes > 60 || RTC_TimeStruct->Seconds > 60)
        return 0;

    uint8_t buf[3];
    buf[0] = RTC_ByteToBcd2(RTC_TimeStruct->Seconds);
    buf[1] = RTC_ByteToBcd2(RTC_TimeStruct->Minutes);
    buf[2] = RTC_ByteToBcd2(RTC_TimeStruct->Hours);
    i2c_manager_write(RTC_I2C_PORT, RTC_ADDRESS, 0x02, buf, 3);

    return 1;
}

void RTC_GetDate(RTC_DateTypeDef *RTC_DateStruct) {
    uint8_t buf[4];
    i2c_manager_read(RTC_I2C_PORT, RTC_ADDRESS, 0x05, buf, 4);

    RTC_DateStruct->Date    = RTC_Bcd2ToByte(buf[0] & 0x3f);
    RTC_DateStruct->WeekDay = RTC_Bcd2ToByte(buf[1] & 0x07);
    RTC_DateStruct->Month   = RTC_Bcd2ToByte(buf[2] & 0x1f);

    if (buf[2] & 0x80) {
        RTC_DateStruct->Year = 1900 + RTC_Bcd2ToByte(buf[3] & 0xff);
    } else {
        RTC_DateStruct->Year = 2000 + RTC_Bcd2ToByte(buf[3] & 0xff);
    }
}

int RTC_SetDate(RTC_DateTypeDef *RTC_DateStruct) {
    if (RTC_DateStruct == NULL || RTC_DateStruct->WeekDay > 7 ||
        RTC_DateStruct->Date > 31 || RTC_DateStruct->Month > 12)
        return 0;

    uint8_t buf[4];
    buf[0] = RTC_ByteToBcd2(RTC_DateStruct->Date);
    buf[1] = RTC_ByteToBcd2(RTC_DateStruct->WeekDay);

    if (RTC_DateStruct->Year < 2000) {
        buf[2] = RTC_ByteToBcd2(RTC_DateStruct->Month) | 0x80;
        buf[3] = RTC_ByteToBcd2((uint8_t)(RTC_DateStruct->Year % 100));
    } else {
        buf[2] = RTC_ByteToBcd2(RTC_DateStruct->Month) | 0x00;
        buf[3] = RTC_ByteToBcd2((uint8_t)(RTC_DateStruct->Year % 100));
    }

    i2c_manager_write(RTC_I2C_PORT, RTC_ADDRESS, 0x05, buf, 4);

    return 1;
}

int RTC_SetAlarmIRQ_Seconds(int afterSeconds) {
    uint8_t reg_value = RTC_ReadReg(0x01);

    if (afterSeconds < 0) {
        reg_value &= ~(1 << 0);
        RTC_WriteReg(0x01, reg_value);
        RTC_WriteReg(0x0E, 0x03);
        return -1;
    }

    uint8_t type_value = 0x82;
    uint8_t div = 1;
    if (afterSeconds > 255) {
        div = 60;
        type_value = 0x83;
    }

    afterSeconds = (afterSeconds / div) & 0xFF;
    RTC_WriteReg(0x0F, afterSeconds);
    RTC_WriteReg(0x0E, type_value);

    reg_value |= (1 << 0);
    reg_value &= ~(1 << 7);
    RTC_WriteReg(0x01, reg_value);
    return afterSeconds * div;
}

int RTC_SetAlarmIRQ_Time(const RTC_TimeTypeDef *RTC_TimeStruct) {
    uint8_t irq_enable = false;
    uint8_t out_buf[4] = {0x80, 0x80, 0x80, 0x80};

    if (RTC_TimeStruct->Minutes >= 0) {
        irq_enable = true;
        out_buf[0] = RTC_ByteToBcd2(RTC_TimeStruct->Minutes) & 0x7f;
    }

    if (RTC_TimeStruct->Hours >= 0) {
        irq_enable = true;
        out_buf[1] = RTC_ByteToBcd2(RTC_TimeStruct->Hours) & 0x3f;
    }

    for (int i = 0; i < 4; i++) {
        RTC_WriteReg(0x09 + i, out_buf[i]);
        vTaskDelay(2 / portTICK_PERIOD_MS);
    }

    uint8_t reg_value = RTC_ReadReg(0x01);

    if (irq_enable) {
        reg_value |= (1 << 1);
    } else {
        reg_value &= ~(1 << 1);
    }

    RTC_WriteReg(0x01, reg_value);

    return irq_enable ? 1 : 0;
}

int RTC_SetAlarmIRQ_DateTime(const RTC_DateTypeDef *RTC_DateStruct, const RTC_TimeTypeDef *RTC_TimeStruct) {
    uint8_t irq_enable = false;
    uint8_t out_buf[4] = {0x80, 0x80, 0x80, 0x80};

    if (RTC_TimeStruct->Minutes >= 0) {
        irq_enable = true;
        out_buf[0] = RTC_ByteToBcd2(RTC_TimeStruct->Minutes) & 0x7f;
    }

    if (RTC_TimeStruct->Hours >= 0) {
        irq_enable = true;
        out_buf[1] = RTC_ByteToBcd2(RTC_TimeStruct->Hours) & 0x3f;
    }

    if (RTC_DateStruct->Date >= 0) {
        irq_enable = true;
        out_buf[2] = RTC_ByteToBcd2(RTC_DateStruct->Date) & 0x3f;
    }

    if (RTC_DateStruct->WeekDay >= 0) {
        irq_enable = true;
        out_buf[3] = RTC_ByteToBcd2(RTC_DateStruct->WeekDay) & 0x07;
    }

    uint8_t reg_value = RTC_ReadReg(0x01);

    if (irq_enable) {
        reg_value |= (1 << 1);
    } else {
        reg_value &= ~(1 << 1);
    }

    for (int i = 0; i < 4; i++) {
        RTC_WriteReg(0x09 + i, out_buf[i]);
    }
    RTC_WriteReg(0x01, reg_value);

    return irq_enable ? 1 : 0;
}

void RTC_ClearIRQ(void) {
    uint8_t data = RTC_ReadReg(0x01);
    RTC_WriteReg(0x01, data & 0xf3);
}

void RTC_DisableIRQ(void) {
    RTC_ClearIRQ();
    uint8_t data = RTC_ReadReg(0x01);
    RTC_WriteReg(0x01, data & 0xfC);
}

void initialize_sntp(void) {
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
}

void obtain_time(void) {
    initialize_sntp();

    // Wait for time to be set
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 10;
    while (timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(pdMS_TO_TICKS(2000));
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    if (timeinfo.tm_year < (2016 - 1900)) {
        ESP_LOGE(TAG, "Failed to obtain time");
    }
}

void print_time_and_date(RTC_TimeTypeDef *time, RTC_DateTypeDef *date) {
    ESP_LOGI(TAG, "Date: %02d/%02d/%04d", date->Date, date->Month, date->Year);
    ESP_LOGI(TAG, "Time: %02d:%02d:%02d", time->Hours, time->Minutes, time->Seconds);
}

void sync_rtc_with_ntp() {
    // Get current time from RTC
    RTC_TimeTypeDef currentTime;
    RTC_DateTypeDef currentDate;
    RTC_GetTime(&currentTime);
    RTC_GetDate(&currentDate);

    // Print current RTC time
    print_time_and_date(&currentTime, &currentDate);

    // Obtain time from NTP server
    obtain_time();
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    // Print NTP time
    ESP_LOGI(TAG, "NTP Date: %02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
    ESP_LOGI(TAG, "NTP Time: %02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    // Convert NTP time to RTC_TimeTypeDef and RTC_DateTypeDef
    RTC_TimeTypeDef ntpTime;
    ntpTime.Seconds = timeinfo.tm_sec;
    ntpTime.Minutes = timeinfo.tm_min;
    ntpTime.Hours = timeinfo.tm_hour;

    RTC_DateTypeDef ntpDate;
    ntpDate.Date = timeinfo.tm_mday;
    ntpDate.Month = timeinfo.tm_mon + 1;
    ntpDate.Year = timeinfo.tm_year + 1900;

    // Compare NTP time with RTC time
    if (abs(currentTime.Seconds - ntpTime.Seconds) > 1 ||
        currentTime.Minutes != ntpTime.Minutes ||
        currentTime.Hours != ntpTime.Hours ||
        currentDate.Date != ntpDate.Date ||
        currentDate.Month != ntpDate.Month ||
        currentDate.Year != ntpDate.Year) {
        
        ESP_LOGI(TAG, "Updating RTC with NTP time");

        // Set RTC with NTP time
        RTC_SetTime(&ntpTime);
        RTC_SetDate(&ntpDate);
    } else {
        ESP_LOGI(TAG, "RTC time is accurate within 1 second of NTP time");
    }

    // Print updated RTC time
    RTC_GetTime(&currentTime);
    RTC_GetDate(&currentDate);
    print_time_and_date(&currentTime, &currentDate);
}