/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_CHROME_I2C_MAP_H
#define __ZEPHYR_CHROME_I2C_MAP_H

#include <devicetree.h>

#include "config.h"

#define I2C_PORT_ACCEL          I2C_PORT_SENSOR
#define I2C_PORT_SENSOR         NPCX_I2C_PORT0_0
#define I2C_PORT_USB_C0		NPCX_I2C_PORT1_0
#define I2C_PORT_USB_C1		NPCX_I2C_PORT2_0
#define I2C_PORT_USB_1_MIX	NPCX_I2C_PORT3_0
#define I2C_PORT_POWER		NPCX_I2C_PORT5_0
#define I2C_PORT_EEPROM		NPCX_I2C_PORT7_0

#endif /* __ZEPHYR_CHROME_I2C_MAP_H */
