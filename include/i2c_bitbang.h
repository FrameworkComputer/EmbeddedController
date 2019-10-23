/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_I2C_BITBANG_H
#define __CROS_EC_I2C_BITBANG_H

#include "i2c.h"

extern const struct i2c_drv bitbang_drv;

extern const struct i2c_port_t i2c_bitbang_ports[];
extern const unsigned int i2c_bitbang_ports_used;

/* expose static functions for testing */
#ifdef TEST_BUILD
int bitbang_start_cond(const struct i2c_port_t *i2c_port);
void bitbang_stop_cond(const struct i2c_port_t *i2c_port);
int bitbang_write_byte(const struct i2c_port_t *i2c_port, uint8_t byte);
void bitbang_set_started(int val);
#endif

#endif /* __CROS_EC_I2C_BITBANG_H */
