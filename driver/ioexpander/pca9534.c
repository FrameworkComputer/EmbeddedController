/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * NXP PCA9534 I/O expander
 */

#include "i2c.h"
#include "pca9534.h"

static int pca9534_pin_read(const int port, const uint16_t addr_flags,
			    int reg, int pin, int *val)
{
	int ret;
	ret = i2c_read8(port, addr_flags, reg, val);
	*val = (*val & BIT(pin)) ? 1 : 0;
	return ret;
}

static int pca9534_pin_write(const int port, const uint16_t addr_flags,
			     int reg, int pin, int val)
{
	int ret, v;
	ret = i2c_read8(port, addr_flags, reg, &v);
	if (ret != EC_SUCCESS)
		return ret;
	v &= ~BIT(pin);
	if (val)
		v |= 1 << pin;
	return i2c_write8(port, addr_flags, reg, v);
}

int pca9534_get_level(const int port, const uint16_t addr_flags,
		      int pin, int *level)
{
	return pca9534_pin_read(port, addr_flags,
				PCA9534_REG_INPUT, pin, level);
}

int pca9534_set_level(const int port, const uint16_t addr_flags,
		      int pin, int level)
{
	return pca9534_pin_write(port, addr_flags,
				 PCA9534_REG_OUTPUT, pin, level);
}

int pca9534_config_pin(const int port, const uint16_t addr_flags,
		       int pin, int is_input)
{
	return pca9534_pin_write(port, addr_flags,
				 PCA9534_REG_CONFIG, pin, is_input);
}
