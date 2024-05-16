/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "system.h"

#include <zephyr/drivers/gpio.h>

__override void board_hibernate_late(void)
{
	/* b/283037861: Pre-off the 5V power line for hibernate. */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_z1), 0);
	/* It takes around 30ms to release the PP5000 capacitance. */
	udelay(30 * MSEC);

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_ulp), 1);
}
