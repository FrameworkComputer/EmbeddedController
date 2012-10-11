/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Link-specific temp sensor module for Chrome EC */

#include "chip_temp_sensor.h"
#include "config.h"
#include "i2c.h"
#include "peci.h"
#include "temp_sensor.h"
#include "tmp006.h"
#include "util.h"

#define TEMP_PCH_REG_ADDR	((0x41 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_CHARGER_REG_ADDR	((0x43 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_USB_REG_ADDR	((0x46 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_HINGE_REG_ADDR	((0x44 << 1) | I2C_FLAG_BIG_ENDIAN)

#define TEMP_PCH_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_PCH_REG_ADDR)
#define TEMP_CHARGER_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_CHARGER_REG_ADDR)
#define TEMP_USB_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_USB_REG_ADDR)
#define TEMP_HINGE_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_HINGE_REG_ADDR)

/* Temperature sensors data; must be in same order as enum temp_sensor_id. */
const struct temp_sensor_t temp_sensors[TEMP_SENSOR_COUNT] = {
#ifdef CONFIG_TMP006
	{"I2C-USB C-Die", TEMP_SENSOR_TYPE_IGNORED, tmp006_get_val, 0, 7},
	{"I2C-USB C-Object", TEMP_SENSOR_TYPE_IGNORED, tmp006_get_val, 1, 7},
	{"I2C-PCH D-Die", TEMP_SENSOR_TYPE_BOARD, tmp006_get_val, 2, 7},
	{"I2C-PCH D-Object", TEMP_SENSOR_TYPE_CASE, tmp006_get_val, 3, 7},
	{"I2C-Hinge C-Die", TEMP_SENSOR_TYPE_IGNORED, tmp006_get_val, 4, 7},
	{"I2C-Hinge C-Object", TEMP_SENSOR_TYPE_IGNORED, tmp006_get_val, 5, 7},
	{"I2C-Charger D-Die", TEMP_SENSOR_TYPE_BOARD, tmp006_get_val, 6, 7},
	{"I2C-Charger D-Object", TEMP_SENSOR_TYPE_CASE, tmp006_get_val, 7, 7},
#endif
#ifdef CONFIG_TASK_TEMPSENSOR
	{"ECInternal", TEMP_SENSOR_TYPE_BOARD, chip_temp_sensor_get_val, 0, 4},
#endif
#ifdef CONFIG_PECI
	{"PECI", TEMP_SENSOR_TYPE_CPU, peci_temp_sensor_get_val, 0, 2},
#endif
};

const struct tmp006_t tmp006_sensors[TMP006_COUNT] = {
	/* TODO: Calibrate sensitivity factors. See crosbug.com/p/9599 */
	{"USB C", TEMP_USB_ADDR, 3.648 * 1e-14f},
	{"PCH D", TEMP_PCH_ADDR, 9.301 * 1e-14f},
	{"Hinge C", TEMP_HINGE_ADDR, -11.000 * 1e-14f},
	{"Charger D", TEMP_CHARGER_ADDR, 10.426 * 1e-14f},
};
