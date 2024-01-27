/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UM_PPM_SMBUS_USERMODE_H_
#define UM_PPM_SMBUS_USERMODE_H_

#include "include/smbus.h"

/**
 * Open a usermode SMBus connection and return the driver.
 *
 * @param bus_num: Corresponds to /dev/i2c-${BUS_NUM}
 * @param chip_address: What chip address to open smbus operations on.
 * @param gpio_chip: Which gpiochip has the smbus alert line?
 * @param gpio_line: What line on that gpiochip has the smbus alert?
 *
 * @return Smbus driver for chosen bus + chip + gpio (alert#) or NULL on error.
 */
struct smbus_driver *smbus_um_open(int bus_num, uint8_t chip_address,
				   int gpio_chip, int gpio_line);

#endif // UM_PPM_SMBUS_USERMODE_H_
