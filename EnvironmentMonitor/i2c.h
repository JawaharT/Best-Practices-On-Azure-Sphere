#pragma once

#include <stdbool.h>
#include "epoll_timerfd_utilities.h"

#define LSM6DSO_ID         0x6C   // register value
#define LSM6DSO_ADDRESS	   0x6A	  // I2C Address

typedef struct {
	float acceleration_mg[3];
	float angular_rate_dps[3];
	float lps22hhpressure_hPa;
	float lps22hhTemperature_degC;
} sensor_var;

sensor_var initI2c(void);
void closeI2c(void);

// Export to use I2C in other file
extern int i2cFd;
extern sensor_var sensor_info;