/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* F75303 temperature sensor module for Chrome EC */

#ifndef __CROS_EC_F75303_H
#define __CROS_EC_F75303_H

#include "i2c.h"

#ifdef BOARD_MUSHU
#define F75303_I2C_ADDR_FLAGS 0x4D
#else
#define F75303_I2C_ADDR_FLAGS 0x4C
#endif

/*
 * I2C port and address information for all the board F75303 sensors should be
 * defined in an array of the following structures, with an enum f75303_sensor
 * indexing the array.  The enum f75303_sensor shall end with a F75303_IDX_COUNT
 * defining the maximum number of sensors for the board.
 */

struct f75303_sensor_t {
	int i2c_port;
	int i2c_addr_flags;
};

extern const struct f75303_sensor_t f75303_sensors[];

enum f75303_index {
	F75303_IDX_LOCAL = 0,
	F75303_IDX_REMOTE1,
	F75303_IDX_REMOTE2,
	F75303_IDX_COUNT,
};

/* F75303 register */
#define F75303_TEMP_LOCAL 0x00
#define F75303_TEMP_REMOTE1 0x01
#define F75303_TEMP_REMOTE2 0x23

#define F75303_ALERT_CHANNEL_MASK 0x1F
#define F75303_DP2_MASK BIT(2)
#define F75303_DP1_MASK BIT(1)
#define F75303_LOCAL_MASK BIT(0)

/**
 * Get the last polled value of a sensor.
 *
 * @param idx	Index to read. Idx indicates whether to read die
 *		temperature or external temperature.
 * @param temp	Destination for temperature in K.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int f75303_get_val(int idx, int *temp);

#ifdef CONFIG_ZEPHYR
void f75303_update_temperature(int idx);
#endif /* CONFIG_ZEPHYR */

#endif /* __CROS_EC_F75303_H */
