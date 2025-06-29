#ifndef _IR_BOARD_ARDUINO_H
#define _IR_BOARD_ARDUINO_H

#include "Arduino.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern lv_obj_t *sensor_label;
extern EventGroupHandle_t pololu_event_group;
#define POLOLU_COMPLETE_BIT BIT0

void init_arduino_i2c_wire();
void i2c_get_status();
void i2c_get_sensors();
void i2c_lvgl_task(void *pvParameter);
void i2c_pololu_command(float command);
void pololu_heartbeat_task(void *pvParameter);

#ifdef __cplusplus
}
#endif

#endif // _IR_BOARD_ARDUINO_H
