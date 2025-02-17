/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "system.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys_clock.h>

__override void board_hibernate_late(void)
{
#if defined(CONFIG_BOARD_SKYWALKER) || defined(CONFIG_BOARD_LUUKE)
	/* b/283037861, b/397139423:
	 * Skywalker reference board only, Pre-off the 5V power line for
	 * hibernate.
	 */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_z1), 0);
#endif
	/* It takes around 30ms to release the PP5000 capacitance. */
	k_busy_wait(30 * USEC_PER_MSEC); /* 30ms */

#ifdef CONFIG_SOC_IT8XXX2
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_ulp), 1);
#endif
}
