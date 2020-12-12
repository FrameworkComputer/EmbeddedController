/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C peripheral interface for Chrome EC */

#ifndef __CROS_EC_I2C_PERIPHERAL_H
#define __CROS_EC_I2C_PERIPHERAL_H

/* Data structure to define I2C peripheral port configuration. */
struct i2c_periph_port_t {
	const char *name;     /* Port name */
	int port;             /* Port */
	uint8_t addr;         /* address(7-bit without R/W) */
};

extern const struct i2c_periph_port_t i2c_periph_ports[];
extern const unsigned int i2c_periphs_used;

#endif /* __CROS_EC_I2C_PERIPHERAL_H */
