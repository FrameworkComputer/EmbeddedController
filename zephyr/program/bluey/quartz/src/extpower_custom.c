/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "extpower.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"

int board_extpower_is_present(void)
{
	/* The pins are ACTIVE_LOW in DTS, so gpio_pin_get_dt returns 1 when
	 * connected. */
	return gpio_pin_get_dt(
		       GPIO_DT_FROM_NODELABEL(gpio_smb2360_a_chg_led_pg_odl)) ||
	       gpio_pin_get_dt(
		       GPIO_DT_FROM_NODELABEL(gpio_smb2360_b_chg_led_pg_odl));
}

void board_extpower_enable_interrupt(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_extpower_a));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_extpower_b));
}

void board_extpower_disable_interrupt(void)
{
	gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_extpower_a));
	gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_extpower_b));
}
