/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common code for VARIANT_OCTOPUS_EC_NPCX796FB configuration */

#include "gpio.h"
#include "i2c.h"
#include "util.h"

/******************************************************************************/
/* I2C port map configuration */
const struct i2c_port_t i2c_ports[] = {
	{"battery", I2C_PORT_BATTERY, 100, GPIO_I2C0_SCL, GPIO_I2C0_SDA},
	{"tcpc0",   I2C_PORT_TCPC0,   100, GPIO_I2C1_SCL, GPIO_I2C1_SDA},
	{"tcpc1",   I2C_PORT_TCPC1,   100, GPIO_I2C2_SCL, GPIO_I2C2_SDA},
	{"eeprom",  I2C_PORT_EEPROM,  100, GPIO_I2C3_SCL, GPIO_I2C3_SDA},
	{"charger", I2C_PORT_CHARGER, 100, GPIO_I2C4_SCL, GPIO_I2C4_SDA},
	{"sensor",  I2C_PORT_SENSOR,  100, GPIO_I2C7_SCL, GPIO_I2C7_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);
