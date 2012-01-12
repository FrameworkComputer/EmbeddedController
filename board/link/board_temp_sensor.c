/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Link-specific temp sensor module for Chrome EC */

#include "temp_sensor.h"
#include "chip_temp_sensor.h"
#include "board.h"
#include "i2c.h"

#define TEMP_CPU_REG_ADDR ((0x40 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_PCH_REG_ADDR ((0x41 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_DDR_REG_ADDR ((0x43 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_CHARGER_REG_ADDR ((0x45 << 1) | I2C_FLAG_BIG_ENDIAN)

#define TEMP_CPU_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_CPU_REG_ADDR)
#define TEMP_PCH_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_PCH_REG_ADDR)
#define TEMP_DDR_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_DDR_REG_ADDR)
#define TEMP_CHARGER_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_CHARGER_REG_ADDR)

/* Temperature sensors data. Must be in the same order as enum
 * temp_sensor_id.
 */
const struct temp_sensor_t temp_sensors[TEMP_SENSOR_COUNT] = {
	{"CPU", TEMP_SENSOR_I2C_DIE_NEAR_CPU, TEMP_CPU_ADDR,
		temp_sensor_tmp006_read, temp_sensor_tmp006_print},
	{"PCH", TEMP_SENSOR_I2C_DIE_NEAR_PCH, TEMP_PCH_ADDR,
		temp_sensor_tmp006_read, temp_sensor_tmp006_print},
	{"DDR", TEMP_SENSOR_I2C_DIE_NEAR_DDR, TEMP_DDR_ADDR,
		temp_sensor_tmp006_read, temp_sensor_tmp006_print},
	{"Charger", TEMP_SENSOR_I2C_DIE_NEAR_CHARGER, TEMP_CHARGER_ADDR,
		temp_sensor_tmp006_read, temp_sensor_tmp006_print},
	{"ECInternal", TEMP_SENSOR_EC_INTERNAL, TEMP_SENSOR_NO_ADDR,
		chip_temp_sensor_read, TEMP_SENSOR_NO_PRINT},
};
