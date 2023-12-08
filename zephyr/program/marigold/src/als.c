/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "als.h"
#include "cros_als.h"
#include "common.h"
#include "driver/als_cm32183.h"
#include "ec_commands.h"
#include "i2c.h"
#include "timer.h"

/*
 * Read CM32183 light sensor data.
 */
int cm32183_read_lux(int *lux, int af)
{
	int ret;
	int data;

	ret = i2c_read16(I2C_PORT_ALS, CM32183_I2C_ADDR,
		CM32183_REG_ALS_RESULT, &data);

	if (ret)
		return ret;

	/*
	 * lux = data * 0.016
	 * lux = (data * 16 / 1000) * af / 10;
	 */
	*lux = data * af * 16 / 10000;

	return EC_SUCCESS;
}

/**
 * Initialise CM32183 light sensor.
 */
int cm32183_init(void)
{
	int retry_count;

	for (retry_count = 0; retry_count < 3; retry_count++) {

		/**
		 * The resume hook does not match the sensor power on sequence,
		 * it will cause the sensor initial fail and disable the als task.
		 * Add the delay time 10ms to retry again if sensor enable fail,
		 * make sure the sensor and als task are enable.
		 */
		if (i2c_write16(I2C_PORT_ALS, CM32183_I2C_ADDR,
			CM32183_REG_CONFIGURE, CM32183_REG_CONFIGURE_CH_EN))
			k_msleep(10);
		else
			return EC_SUCCESS;
	}

	return EC_ERROR_OVERFLOW;

}

struct als_t als[] = {
	{"CAPELLA", cm32183_init, cm32183_read_lux, 32},
};
BUILD_ASSERT(ARRAY_SIZE(als) == ALS_COUNT);
