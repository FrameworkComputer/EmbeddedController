/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_I2C_STM32F0_H
#define __CROS_EC_I2C_STM32F0_H

/**
 * Initialize on the specified I2C port.
 *
 * @param p		the I2c port
 */
void stm32f0_i2c_init_port(const struct i2c_port_t *p);

#endif
