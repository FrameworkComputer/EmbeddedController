/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C slave interface for Chrome EC */

#ifndef __CROS_EC_I2CSLV_H
#define __CROS_EC_I2CSLV_H

/* Data structure to define I2C slave port configuration. */
struct i2c_slv_port_t {
	const char *name;     /* Port name */
	int port;             /* Port */
	uint8_t slave_adr;    /* slave address(7-bit without R/W) */
};

extern const struct i2c_slv_port_t i2c_slv_ports[];
extern const unsigned int i2c_slvs_used;

#endif /* __CROS_EC_I2CSLV_H */
