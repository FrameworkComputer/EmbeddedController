/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* STM32 common I2C code for Chrome EC */

#include "i2c.h"
#include "util.h"

/* Maximum transfer of a SMBUS block transfer */
#define SMBUS_MAX_BLOCK 32

int i2c_read_string(int port, int slave_addr,
	int offset, uint8_t *data, int len)
{
	int rv;
	uint8_t reg, block_length;

	if ((len <= 0) || (len > SMBUS_MAX_BLOCK))
		return EC_ERROR_INVAL;

	i2c_lock(port, 1);

	/* Read the counted string into the output buffer */
	reg = offset;
	rv = i2c_xfer(port, slave_addr, &reg, 1, data, len, I2C_XFER_SINGLE);
	if (rv == EC_SUCCESS) {
		/* Block length is the first byte of the returned buffer */
		block_length = MIN(data[0], len - 1);

		/* Move data down, then null-terminate it */
		memmove(data, data + 1, block_length);
		data[block_length] = 0;
	}

	i2c_lock(port, 0);
	return rv;
}
