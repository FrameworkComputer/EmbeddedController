/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "i2c.h"
#include "pi3usb9201.h"

#define PI3USB9201_ADDR	0x5f

inline void init_pi3usb9201(void)
{
	/*
	 * Write 0x08 (Client mode detection and Enable USB switch auto ON) to
	 *	control Reg 2
	 * Write 0x08 (Client Mode) to Control Reg 1
	 */
	i2c_write16(1, PI3USB9201_ADDR, CTRL_REG1, 0x0808);
}

inline void write_pi3usb9201(enum pi3usb9201_reg_t reg,
					enum pi3usb9201_dat_t dat)
{
	i2c_write8(1, PI3USB9201_ADDR, reg, dat);
}

inline uint8_t read_pi3usb9201(enum pi3usb9201_reg_t reg)
{
	int tmp;

	i2c_read8(1, PI3USB9201_ADDR, reg, &tmp);

	return tmp;
}
