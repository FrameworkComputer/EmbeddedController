/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* F75397 temperature sensor module for Chrome EC */

#ifndef __CROS_EC_F75397_H
#define __CROS_EC_F75397_H

#include "i2c.h"

#define F75397_I2C_ADDR_FLAGS		0x4C

/*
 * I2C port and address information for all the board F75397 sensors should be
 * defined in an array of the following structures, with an enum f75397_sensor
 * indexing the array.  The enum f75397_sensor shall end with a F75397_IDX_COUNT
 * defining the maximum number of sensors for the board.
 */

struct f75397_sensor_t {
	int i2c_port;
	int i2c_addr_flags;
};

extern const struct f75397_sensor_t f75397_sensors[];

enum f75397_index {
	F75397_IDX_LOCAL = 0,
	F75397_IDX_REMOTE1,
	F75397_IDX_COUNT,
};

/* F75397 register */
#define F75397_TEMP_LOCAL		0x00
#define F75397_TEMP_LOCAL_LOW_REGISTER 0x29
#define F75397_TEMP_REMOTE1		0x01
#define F75397_TEMP_REMOTE1_LOW_REGISTER 0x10

/**
 * Get the last polled value of a sensor.
 *
 * @param idx	Index to read. Idx indicates whether to read die
 *		temperature or external temperature.
 * @param temp	Destination for temperature in K.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int f75397_get_val(int idx, int *temp);

/**
 * Get the last polled value of a sensor.
 *
 * @param idx	Index to read. Idx indicates whether to read die
 *		temperature or external temperature.
 * @param temp	Destination for temperature in mK.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int f75397_get_val_k(int idx, int *temp_k_ptr);

/**
 * Get the last polled value of a sensor.
 *
 * @param idx		Index to read, from board's enum f75397_sensor
 *			definition
 *
 * @param temp_mk_ptr	Destination for temperature in mK.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int f75397_get_val_mk(int idx, int *temp_mk_ptr);

/**
 * Set if the underlying polling task will read the sensor
 * or if it will skip, as the rail this sensor is on
 * may sometimes be powered off
 *
 * @param enabled	Set if the sensor should be polled or skipped
 */
void f75397_set_enabled(uint8_t enabled);

#ifdef CONFIG_ZEPHYR
void f75397_update_temperature(int idx);
#endif /* CONFIG_ZEPHYR */

#endif  /* __CROS_EC_F75397_H */
