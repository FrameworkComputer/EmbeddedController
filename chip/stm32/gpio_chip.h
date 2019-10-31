/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CHIP_STM32_GPIO_CHIP_H
#define __CROS_EC_CHIP_STM32_GPIO_CHIP_H

#include "include/gpio.h"

/**
 * Enable GPIO peripheral clocks.
 */
void gpio_enable_clocks(void);

/**
 * Return gpio port clocks that are necessary to support
 * the pins in gpio.inc.
 */
int gpio_required_clocks(void);

#endif  /* __CROS_EC_CHIP_STM32_GPIO_CHIP_H */
