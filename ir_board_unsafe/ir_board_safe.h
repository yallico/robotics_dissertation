#ifndef IR_BOARD_SAFE_H
#define IR_BOARD_SAFE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "driver/i2c_master.h"

// Define I2C port as 1
#if defined (CONFIG_I2C_MANAGER_1_ENABLED)
	#define DUPLEX_I2C	1
	#define DUPLEX_BOARD_ADDR 0x08
	#if defined (CONFIG_I2C_MANAGER_1_PULLUPS)
		#define I2C_MANAGER_1_PULLUPS 	true
	#else
		#define I2C_MANAGER_1_PULLUPS 	false
  #endif
#endif

// Small struct used to change mode.
// It's also 1 byte, so convenient to
// pass around 1 byte of data.
typedef struct i2c_mode {
  uint8_t mode;
} ir_i2c_mode_t;

typedef struct i2c_status { 
  uint8_t mode;                   // 1  bytes
  uint16_t fail_count[4];          // 8 bytes
  uint16_t pass_count[4];         // 8 bytes 
} i2c_status_t;

typedef struct i2c_sensors {
  int16_t ldr[3];     // 6 bytes
  int16_t prox[2];    // 4 bytes
} i2c_sensors_t;


void i2c_external_master_init();
void i2c_get_status(i2c_status_t *status);
void i2c_get_sensors(i2c_sensors_t *sensors);

#define MODE_REPORT_STATUS  0
#define MODE_STOP_TX        1
#define MODE_REPORT_LDR0    2
#define MODE_REPORT_LDR1    3
#define MODE_REPORT_LDR2    4
#define MODE_SIZE_MSG0    5
#define MODE_SIZE_MSG1    6
#define MODE_SIZE_MSG2    7
#define MODE_SIZE_MSG3    8
#define MODE_REPORT_MSG0    9
#define MODE_REPORT_MSG1    10
#define MODE_REPORT_MSG2    11
#define MODE_REPORT_MSG3    12
#define MODE_CLEAR_MSG0     13
#define MODE_CLEAR_MSG1     14
#define MODE_CLEAR_MSG2     15
#define MODE_CLEAR_MSG3     16
#define MODE_REPORT_SENSORS 17
#define MODE_RESET_COUNTS   18
#define MODE_REPORT_RX_ACTIVITY 19
#define MODE_REPORT_RX_DIRECTION 20
#define MODE_REPORT_TIMINGS 21
#define MODE_REPORT_HIST    22
#define MODE_CLEAR_HIST     23
#define MAX_MODE            24


#ifdef __cplusplus
}
#endif
#endif
