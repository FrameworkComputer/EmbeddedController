/*
 * Copyright 2023 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/gpio.h>

#include "factory.h"
#include "gpio.h"
#include "gpio/gpio_int.h"

static uint8_t factory_enable;

void factory_setting(uint8_t enable)
{
	factory_enable = enable;

	/* Ignored the power button signal if we are in the factory mode */
	if (factory_enable)
		gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_powerbtn));
	else
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_powerbtn));
}
