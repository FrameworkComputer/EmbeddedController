/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Intersil ILS29035 light sensor driver
 */

#include "driver/als_isl29035.h"
#include "common.h"
#include "hooks.h"
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

static void isl29035_init(void)
{
	/*
	 * Tell it to read continually. This uses 70uA, as opposed to nearly
	 * zero, but it makes the hook/update code cleaner (we don't want to
	 * wait 90ms to read on demand while processing hook callbacks).
	 */
	(void)i2c_write8(I2C_PORT_ALS, ILS29035_I2C_ADDR,
			 ILS29035_REG_COMMAND_I, 0xa0);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, isl29035_init, HOOK_PRIO_DEFAULT);

int isl29035_read_lux(int *lux, int af)
{
	int rv, lsb, msb, data;

	/*
	 * NOTE: It is necessary to read the LSB first, then the MSB. If you do
	 * it in the opposite order, the results are not correct. This is
	 * apparently an undocumented "feature". It's especially noticeable in
	 * one-shot mode.
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
	 * 0x0000-0xffff indicates 0-1000 lux. We multiply the sensor value by
	 * a scaling factor to account for attentuation by glass, tinting, etc.
	 *
	 * Caution: Don't go nuts with the attentuation factor. If it's
	 * greater than 32, the signed int math will roll over and you'll get
	 * very wrong results. Of course, if you have that much attenuation and
	 * are still getting useful readings, you probably have your sensor
	 * pointed directly into the sun.
	 */
	*lux = data * af * 1000 / 0xffff;

	return EC_SUCCESS;
}
