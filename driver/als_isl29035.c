/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Intersil ILS29035 light sensor driver
 */

#include "driver/als_isl29035.h"
#include "common.h"
#include "i2c.h"
#include "timer.h"

/* I2C interface */
#define ILS29035_I2C_ADDR       0x88
#define ILS29035_REG_COMMAND_I  0
#define ILS29035_REG_COMMAND_II 1
#define ILS29035_REG_DATA_LSB   2
#define ILS29035_REG_DATA_MSB   3
#define ILS29035_REG_INT_LT_LSB 4
#define ILS29035_REG_INT_LT_MSB 5
#define ILS29035_REG_INT_HT_LSB 6
#define ILS29035_REG_INT_HT_MSB 7
#define ILS29035_REG_ID         15

int isl29035_read_lux(int *lux)
{
	int rv, lsb, msb, data;

	/* Tell it to read once */
	rv = i2c_write8(I2C_PORT_ALS, ILS29035_I2C_ADDR,
			ILS29035_REG_COMMAND_I, 0x20);
	if (rv)
		return rv;

	/* The highest precision (default) should take ~90ms */
	usleep(100 * MSEC);

	/* NOTE: It is necessary to read the LSB first, then the MSB. If you do
	 * it in the opposite order, the results are not correct. This is
	 * apparently an undocumented "feature".
	 */

	/* Read lsb */
	rv = i2c_read8(I2C_PORT_ALS, ILS29035_I2C_ADDR,
		       ILS29035_REG_DATA_LSB, &lsb);
	if (rv)
		return rv;

	/* Read msb */
	rv = i2c_read8(I2C_PORT_ALS, ILS29035_I2C_ADDR,
		       ILS29035_REG_DATA_MSB, &msb);
	if (rv)
		return rv;

	data = (msb << 8) | lsb;

	/*
	 * The default power-on values will give 16 bits of precision:
	 * 0x0000-0xffff indicates 0-1000 lux. If you change the defaults,
	 * you'll need to change the scale factor accordingly.
	 */
	*lux = data * 1000 / 0xffff;

	return EC_SUCCESS;
}
