#ifndef IR_BOARD_H
#define IR_BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "i2c_manager.h"

// Define your I2C port
#define DUPLEX_I2C	(i2c_port_t)I2C_NUM_1
#define DUPLEX_BOARD_ADDR 0x08

typedef struct i2c_status { 
  uint8_t mode;                   // 1  bytes
  uint16_t fail_count[4];          // 8 bytes
  uint16_t pass_count[4];         // 8 bytes 
} i2c_status_t;

typedef struct i2c_sensors {
  int16_t ldr[3];     // 6 bytes
  int16_t prox[2];    // 4 bytes
} i2c_sensors_t;


esp_err_t ir_board_init(void);
void ir_write(uint8_t data);
void print_status(i2c_status_t *status);
void ir_get_status(i2c_status_t *status, uint8_t data);

#ifdef __cplusplus
}
#endif
#endif
