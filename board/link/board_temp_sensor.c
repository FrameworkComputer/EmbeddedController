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

#define TEMP_CPU_REG_ADDR ((0x40 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_PCH_REG_ADDR ((0x41 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_DDR_REG_ADDR ((0x45 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_CHARGER_REG_ADDR ((0x43 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_U15_REG_ADDR ((0x47 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_U20_REG_ADDR ((0x46 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_U27_REG_ADDR ((0x44 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_U29_REG_ADDR ((0x42 << 1) | I2C_FLAG_BIG_ENDIAN)

#define TEMP_CPU_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_CPU_REG_ADDR)
#define TEMP_PCH_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_PCH_REG_ADDR)
#define TEMP_DDR_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_DDR_REG_ADDR)
#define TEMP_CHARGER_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_CHARGER_REG_ADDR)
#define TEMP_U15_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_U15_REG_ADDR)
#define TEMP_U20_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_U20_REG_ADDR)
#define TEMP_U27_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_U27_REG_ADDR)
#define TEMP_U29_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_U29_REG_ADDR)

/* Temperature sensors data. Must be in the same order as enum
 * temp_sensor_id.
 */
const struct temp_sensor_t temp_sensors[TEMP_SENSOR_COUNT] = {
#ifdef CONFIG_TMP006
	{"I2C_CPU-Die", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_CPU,
	 tmp006_get_val, 0, 7},
	{"I2C_CPU-Object", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_IGNORED,
	 tmp006_get_val, 1, 7},
	{"I2C_PCH-Die", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_BOARD,
	 tmp006_get_val, 2, 7},
	{"I2C_PCH-Object", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_CASE,
	 tmp006_get_val, 3, 7},
	{"I2C_DDR-Die", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_BOARD,
	 tmp006_get_val, 4, 7},
	{"I2C_DDR-Object", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_CASE,
	 tmp006_get_val, 5, 7},
	{"I2C_Charger-Die", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_BOARD,
	 tmp006_get_val, 6, 7},
	{"I2C_Charger-Object", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_CASE,
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
	{"I2C_U15-Die", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_IGNORED,
	 tmp006_get_val, 8, 7},
	{"I2C_U15-Object", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_IGNORED,
	 tmp006_get_val, 9, 7},
	{"I2C_U20-Die", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_IGNORED,
	 tmp006_get_val, 10, 7},
	{"I2C_U20-Object", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_IGNORED,
	 tmp006_get_val, 11, 7},
	{"I2C_U27-Die", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_IGNORED,
	 tmp006_get_val, 12, 7},
	{"I2C_U27-Object", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_IGNORED,
	 tmp006_get_val, 13, 7},
	{"I2C_U29-Die", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_IGNORED,
	 tmp006_get_val, 14, 7},
	{"I2C_U29-Object", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_IGNORED,
	 tmp006_get_val, 15, 7},
#endif
};

const struct tmp006_t tmp006_sensors[TMP006_COUNT] = {
	/* TODO: Calibrate sensitivity factors. See crosbug.com/p/9599 */
	{"CPU", TEMP_CPU_ADDR, 2771},
	{"PCH", TEMP_PCH_ADDR, 14169},
	{"DDR", TEMP_DDR_ADDR, 6400},
	{"Charger", TEMP_CHARGER_ADDR, 10521},
	{"U15", TEMP_U15_ADDR, 6400},
	{"U20", TEMP_U20_ADDR, 6400},
	{"U27", TEMP_U27_ADDR, 6400},
	{"U29", TEMP_U29_ADDR, 6400},
};
