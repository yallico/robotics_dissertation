#ifndef I2C_SLAVE_H
#define I2C_SLAVE_H

#include <Wire.h>

void onReceiveI2C(int bytes); // forward declaration

inline void i2c_slave_init(uint8_t address) {
  Wire.begin(address);
  Wire.onReceive(onReceiveI2C);
}

#endif