/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Link-specific temp sensor module for Chrome EC */

#include "temp_sensor.h"
#include "chip_temp_sensor.h"
#include "board.h"
#include "i2c.h"
#include "peci.h"
#include "tmp006.h"
#include "util.h"

#define TEMP_HEATPIPE_REG_ADDR	((0x40 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_PCH_REG_ADDR	((0x41 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_MEMORY_REG_ADDR	((0x45 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_CHARGER_REG_ADDR	((0x43 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_DCJACK_REG_ADDR	((0x47 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_USB_REG_ADDR	((0x46 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_HINGE_REG_ADDR	((0x44 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_SDCARD_REG_ADDR	((0x42 << 1) | I2C_FLAG_BIG_ENDIAN)

#define TEMP_HEATPIPE_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_HEATPIPE_REG_ADDR)
#define TEMP_PCH_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_PCH_REG_ADDR)
#define TEMP_MEMORY_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_MEMORY_REG_ADDR)
#define TEMP_CHARGER_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_CHARGER_REG_ADDR)
#define TEMP_DCJACK_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_DCJACK_REG_ADDR)
#define TEMP_USB_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_USB_REG_ADDR)
#define TEMP_HINGE_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_HINGE_REG_ADDR)
#define TEMP_SDCARD_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_SDCARD_REG_ADDR)

/* Temperature sensors data. Must be in the same order as enum
 * temp_sensor_id.
 */
const struct temp_sensor_t temp_sensors[TEMP_SENSOR_COUNT] = {
#ifdef CONFIG_TMP006
	{"I2C-Heat Pipe D-Die", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_BOARD,
	 tmp006_get_val, 0, 7},
	{"I2C-Heat Pipe D-Object", TEMP_SENSOR_POWER_VS,
	 TEMP_SENSOR_TYPE_IGNORED, tmp006_get_val, 1, 7},
	{"I2C-PCH D-Die", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_BOARD,
	 tmp006_get_val, 2, 7},
	{"I2C-PCH D-Object", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_CASE,
	 tmp006_get_val, 3, 7},
	{"I2C-Memory D-Die", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_BOARD,
	 tmp006_get_val, 4, 7},
	{"I2C-Memory D-Object", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_CASE,
	 tmp006_get_val, 5, 7},
	{"I2C-Charger D-Die", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_BOARD,
	 tmp006_get_val, 6, 7},
	{"I2C-Charger D-Object", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_CASE,
	 tmp006_get_val, 7, 7},
#endif
#ifdef CONFIG_TASK_TEMPSENSOR
	{"ECInternal", TEMP_SENSOR_POWER_NONE, TEMP_SENSOR_TYPE_BOARD,
	 chip_temp_sensor_get_val, 0, 4},
#endif
#ifdef CONFIG_PECI
	{"PECI", TEMP_SENSOR_POWER_CPU, TEMP_SENSOR_TYPE_CPU,
	 peci_temp_sensor_get_val, 0, 2},
#endif
#ifdef CONFIG_TMP006
	{"I2C-DCJack C-Die", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_IGNORED,
	 tmp006_get_val, 8, 7},
	{"I2C-DCJack C-Object", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_IGNORED,
	 tmp006_get_val, 9, 7},
	{"I2C-USB C-Die", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_IGNORED,
	 tmp006_get_val, 10, 7},
	{"I2C-USB C-Object", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_IGNORED,
	 tmp006_get_val, 11, 7},
	{"I2C-Hinge C-Die", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_IGNORED,
	 tmp006_get_val, 12, 7},
	{"I2C-Hinge C-Object", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_IGNORED,
	 tmp006_get_val, 13, 7},
	{"I2C-SDCard D-Die", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_IGNORED,
	 tmp006_get_val, 14, 7},
	{"I2C-SDCard D-Object", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_IGNORED,
	 tmp006_get_val, 15, 7},
#endif
};

const struct tmp006_t tmp006_sensors[TMP006_COUNT] = {
	/* TODO: Calibrate sensitivity factors. See crosbug.com/p/9599 */
	{"Heat pipe D", TEMP_HEATPIPE_ADDR, 2771},
	{"PCH D", TEMP_PCH_ADDR, 14169},
	{"Memory D", TEMP_MEMORY_ADDR, 6400},
	{"Charger D", TEMP_CHARGER_ADDR, 10521},
	{"DCJack C", TEMP_DCJACK_ADDR, 6400},
	{"USB C", TEMP_USB_ADDR, 6400},
	{"Hinge C", TEMP_HINGE_ADDR, 6400},
	{"SD Card D", TEMP_SDCARD_ADDR, 6400},
};
