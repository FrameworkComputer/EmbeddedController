/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI LM3509 LED driver.
 */

#include "i2c.h"
#include "lm3509.h"

inline int lm3509_write(uint8_t reg, uint8_t val)
{
	return i2c_write8(I2C_PORT_KBLIGHT, LM3509_I2C_ADDR, reg, val);
}

int lm3509_poweron(void)
{
	int ret = 0;

	/* BIT= description
	 * [2]= set both main and seconfary current same, both control by BMAIN.
	 * [1]= enable secondary current sink.
	 * [0]= enable main current sink.
	 */
	ret |= lm3509_write(LM3509_REG_GP, 0x07);
	/* Brigntness register
	 * 0x00= 0%
	 * 0x1F= 100%
	 */
	ret |= lm3509_write(LM3509_REG_BMAIN, 0x1F);

	return ret;
}

int lm3509_poweroff(void)
{
	int ret = 0;

	ret |= lm3509_write(LM3509_REG_GP, 0x00);
	ret |= lm3509_write(LM3509_REG_BMAIN, 0x00);

	return ret;
}
