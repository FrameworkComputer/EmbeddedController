/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * NXP PCA9534 I/O expander
 */

#include "i2c.h"
#include "ioexpander_pca9534.h"

static int pca9534_pin_read__7bf(const int port, const uint16_t addr__7bf,
			    int reg, int pin, int *val)
{
	int ret;
	ret = i2c_read8__7bf(port, addr__7bf, reg, val);
	*val = (*val & BIT(pin)) ? 1 : 0;
	return ret;
}

static int pca9534_pin_write__7bf(const int port, const uint16_t addr__7bf,
			     int reg, int pin, int val)
{
	int ret, v;
	ret = i2c_read8__7bf(port, addr__7bf, reg, &v);
	if (ret != EC_SUCCESS)
		return ret;
	v &= ~BIT(pin);
	if (val)
		v |= 1 << pin;
	return i2c_write8__7bf(port, addr__7bf, reg, v);
}

int pca9534_get_level__7bf(const int port, const uint16_t addr__7bf,
		      int pin, int *level)
{
	return pca9534_pin_read__7bf(port, addr__7bf,
				PCA9534_REG_INPUT, pin, level);
}

int pca9534_set_level__7bf(const int port, const uint16_t addr__7bf,
		      int pin, int level)
{
	return pca9534_pin_write__7bf(port, addr__7bf,
				 PCA9534_REG_OUTPUT, pin, level);
}

int pca9534_config_pin__7bf(const int port, const uint16_t addr__7bf,
		       int pin, int is_input)
{
	return pca9534_pin_write__7bf(port, addr__7bf,
				 PCA9534_REG_CONFIG, pin, is_input);
}
