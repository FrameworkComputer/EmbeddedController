/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Temperature sensor module for Chrome EC */

#ifndef __CROS_EC_TEMP_SENSOR_H
#define __CROS_EC_TEMP_SENSOR_H

#include "common.h"
#include "board.h"

/* "enum temp_sensor_id" must be defined for each board in board.h. */
enum temp_sensor_id;

struct temp_sensor_t {
	const char* name;
	/* Sensor address. Used by read and print functions. */
	int addr;
	/* Read sensor value and return temperature in K. */
	int (*read)(const struct temp_sensor_t* self);
	/* Print debug info on console. */
	int (*print)(const struct temp_sensor_t* self);
};

/* Dummy value to put in "addr" field in temp_sensor_t if we don't need to
 * specify address.
 */
#define TEMP_SENSOR_NO_ADDR 0

/* Dummy value to put in "print" field in temp_sensor_t if we don't have debug
 * function for a sensor.
 */
#define TEMP_SENSOR_NO_PRINT 0

/* Initializes the module. */
int temp_sensor_init(void);

/* Returns the most recently measured temperature for the sensor in K,
 * or -1 if error. */
int temp_sensor_read(enum temp_sensor_id id);


#define TMP006_ADDR(PORT,REG) ((PORT << 16) + REG)
#define TMP006_PORT(ADDR) (ADDR >> 16)
#define TMP006_REG(ADDR) (ADDR & 0xffff)

/* Read TI TMP006 die temperature sensor. Return temperature in K. */
int temp_sensor_tmp006_read_die_temp(const struct temp_sensor_t* sensor);

/* Read TI TMP006 object temperature sensor. Return temperature in K. */
int temp_sensor_tmp006_read_object_temp(const struct temp_sensor_t* sensor);

/* Configure TMP006 DRDY pin. */
void temp_sensor_tmp006_config(const struct temp_sensor_t* sensor);

/* Print debug messages for TMP006. */
int temp_sensor_tmp006_print(const struct temp_sensor_t* sensor);

#endif  /* __CROS_EC_TEMP_SENSOR_H */
