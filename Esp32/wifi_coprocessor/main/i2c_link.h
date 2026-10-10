#ifndef WIFI_COPROCESSOR_I2C_LINK_H_
#define WIFI_COPROCESSOR_I2C_LINK_H_

#include "esp_err.h"

esp_err_t I2cLink_Init(void);
void I2cLink_Task(void);

#endif
