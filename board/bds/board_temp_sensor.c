/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* BDS-specific temp sensor module for Chrome EC */

#include "temp_sensor.h"
#include "chip_temp_sensor.h"
#include "board.h"
#include "i2c.h"
#include "tmp006.h"
#include "util.h"

#define TEMP_CASE_DIE_REG_ADDR ((0x40 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_CASE_DIE_ADDR \
	TMP006_ADDR(I2C_PORT_THERMAL, TEMP_CASE_DIE_REG_ADDR)

/* Temperature sensors data. Must be in the same order as enum
 * temp_sensor_id.
 */
const struct temp_sensor_t temp_sensors[TEMP_SENSOR_COUNT] = {
	{"ECInternal", TEMP_SENSOR_POWER_NONE, chip_temp_sensor_get_val, 0},
	{"CaseDie", TEMP_SENSOR_POWER_VS, tmp006_get_val, 0},
	{"Object", TEMP_SENSOR_POWER_VS, tmp006_get_val, 0},
};

const struct tmp006_t tmp006_sensors[TMP006_COUNT] = {
	{"TMP006", TEMP_CASE_DIE_ADDR},
};
