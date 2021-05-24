/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Texas Instruments TDP142 DisplayPort Linear Redriver
 */

#include "common.h"
#include "ec_commands.h"
#include "i2c.h"
#include "tdp142.h"

static enum ec_error_list tdp142_write(int offset, int data)
{
	return i2c_write8(TDP142_I2C_PORT,
			  TDP142_I2C_ADDR,
			  offset, data);

}

static enum ec_error_list tdp142_read(int offset, int *regval)
{
	return i2c_read8(TDP142_I2C_PORT,
			 TDP142_I2C_ADDR,
			 offset, regval);

}

enum ec_error_list tdp142_set_ctlsel(enum tdp142_ctlsel selection)
{
	int regval;
	enum ec_error_list rv;

	rv = tdp142_read(TDP142_REG_GENERAL, &regval);
	if (rv)
		return rv;

	regval &= ~TDP142_GENERAL_CTLSEL;
	regval |= selection;

	return tdp142_write(TDP142_REG_GENERAL, regval);
}
