/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "i2c.h"
#include "tca6424a.h"

int tca6424a_write_bit(int port, enum tca6424a_bank bank, uint8_t bit, int val)
{
	int tmp;
	int ret;

	/* Read output port register */
	ret = i2c_read8(port, TCA6424A_ADDR_FLAGS, bank, &tmp);
	if (ret != EC_SUCCESS)
		return ret;

	if (val)
		tmp |= BIT(bit);
	else
		tmp &= ~BIT(bit);

	/* Write back modified output port register */
	ret = i2c_write8(port, TCA6424A_ADDR_FLAGS, bank, tmp);
	if (ret != EC_SUCCESS)
		return ret;

	return EC_SUCCESS;
}

int tca6424a_write_byte(int port, enum tca6424a_bank bank, uint8_t val)
{
	int ret;

	ret = i2c_write8(port, TCA6424A_ADDR_FLAGS, bank, val);
	if (ret != EC_SUCCESS)
		return ret;

	return EC_SUCCESS;
}

int tca6424a_read_byte(int port, enum tca6424a_bank bank)
{
	int tmp;

	if (i2c_read8(port, TCA6424A_ADDR_FLAGS, bank, &tmp) != EC_SUCCESS)
		return -1;

	return tmp;
}

int tca6424a_read_bit(int port, enum tca6424a_bank bank, uint8_t bit)
{
	int tmp;
	int mask = 1 << bit;

	/* Read input port register */
	if (i2c_read8(port, TCA6424A_ADDR_FLAGS, bank, &tmp) != EC_SUCCESS)
		return -1;

	return (tmp & mask) >> bit;
}
