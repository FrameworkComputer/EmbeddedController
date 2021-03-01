/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_I2C_MAP_H
#define __ZEPHYR_I2C_MAP_H

#include <devicetree.h>

#include "i2c/i2c.h"

#define I2C_PORT_ACCEL		I2C_PORT_SENSOR
#define I2C_PORT_SENSOR		NAMED_I2C(sensor)
#define I2C_PORT_USB_C0		NAMED_I2C(usb_c0)
#define I2C_PORT_USB_C1		NAMED_I2C(usb_c1)
#define I2C_PORT_USB_1_MIX	NAMED_I2C(usb1_mix)
#define I2C_PORT_POWER		NAMED_I2C(power)
#define I2C_PORT_EEPROM		NAMED_I2C(eeprom)

#define I2C_ADDR_EEPROM_FLAGS	0x50
#define I2C_PORT_BATTERY	I2C_PORT_POWER
#define I2C_PORT_CHARGER	I2C_PORT_EEPROM

#endif /* __ZEPHYR_I2C_MAP_H */
