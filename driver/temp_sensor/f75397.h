/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* F75397 temperature sensor module for Chrome EC */

#ifndef __CROS_EC_F75397_H
#define __CROS_EC_F75397_H

#define F75397_I2C_ADDR_FLAGS		0x4C


enum f75397_index {
	F75397_IDX_LOCAL = 0,
	F75397_IDX_REMOTE1,
	F75397_IDX_COUNT,
};

/* F75397 register */
#define F75397_TEMP_LOCAL		0x00
#define F75397_TEMP_REMOTE1		0x01

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
 * Set if the underlying polling task will read the sensor
 * or if it will skip, as the rail this sensor is on
 * may sometimes be powered off
 *
 * @param enabled	Set if the sensor should be polled or skipped
 */
void f75397_set_enabled(uint8_t enabled);

#endif  /* __CROS_EC_F75397_H */
