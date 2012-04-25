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
#ifdef CONFIG_TMP006
	{"I2C_CPU-Die", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_CPU,
	 tmp006_get_val, 0},
	{"I2C_CPU-Object", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_CASE,
	 tmp006_get_val, 1},
	{"I2C_PCH-Die", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_BOARD,
	 tmp006_get_val, 2},
	{"I2C_PCH-Object", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_CASE,
	 tmp006_get_val, 3},
	{"I2C_DDR-Die", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_BOARD,
	 tmp006_get_val, 4},
	{"I2C_DDR-Object", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_CASE,
	 tmp006_get_val, 5},
	{"I2C_Charger-Die", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_BOARD,
	 tmp006_get_val, 6},
	{"I2C_Charger-Object", TEMP_SENSOR_POWER_VS, TEMP_SENSOR_TYPE_CASE,
	 tmp006_get_val, 7},
#endif
#ifdef CONFIG_TASK_TEMPSENSOR
	{"ECInternal", TEMP_SENSOR_POWER_NONE, TEMP_SENSOR_TYPE_BOARD,
	 chip_temp_sensor_get_val, 0},
#endif
#ifdef CONFIG_PECI
	{"PECI", TEMP_SENSOR_POWER_CPU, TEMP_SENSOR_TYPE_CPU,
	 peci_temp_sensor_get_val, 0},
#endif
};

const struct tmp006_t tmp006_sensors[TMP006_COUNT] = {
	{"CPU", TEMP_CPU_ADDR},
	{"PCH", TEMP_PCH_ADDR},
	{"DDR", TEMP_DDR_ADDR},
	{"Charger", TEMP_CHARGER_ADDR},
};
