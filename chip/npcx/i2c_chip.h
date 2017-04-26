/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NPCX-specific I2C module for Chrome EC */

#ifndef __CROS_EC_I2C_CHIP_H
#define __CROS_EC_I2C_CHIP_H

/**
 * Select specific i2c port connected to i2c controller.
 *
 * @parm port I2C port
 */
void i2c_select_port(int port);

/*
 * Due to we couldn't support GPIO reading when IO is selected I2C, we need
 * to distingulish which mode we used currently.
 *
 * @parm port I2C port
 *
 * @return 0: i2c ports are selected to pins. 1: GPIOs are selected to pins.
 */
int i2c_is_raw_mode(int port);

#endif /* __CROS_EC_I2C_CHIP_H */
