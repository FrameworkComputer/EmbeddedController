/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_I2C_MAP_H
#define __ZEPHYR_I2C_MAP_H

#include <devicetree.h>

#include "i2c/i2c.h"

#define I2C_PORT_BATTERY	I2C_PORT_POWER
#define I2C_PORT_VIRTUAL	I2C_PORT_BATTERY
#define I2C_PORT_CHARGER	I2C_PORT_POWER
#define I2C_PORT_ACCEL		I2C_PORT_SENSOR

#define I2C_PORT_POWER		NAMED_I2C(power)
#define I2C_PORT_TCPC0		NAMED_I2C(tcpc0)
#define I2C_PORT_TCPC1		NAMED_I2C(tcpc1)
#define I2C_PORT_WLC		NAMED_I2C(wlc)
#define I2C_PORT_EEPROM		NAMED_I2C(eeprom)
#define I2C_PORT_SENSOR		NAMED_I2C(sensor)

#endif /* __ZEPHYR_I2C_MAP_H */
