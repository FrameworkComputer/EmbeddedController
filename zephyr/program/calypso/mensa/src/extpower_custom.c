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
	/* The pins are ACTIVE_LOW in DTS, so gpio_pin_get_dt returns 0 when
	 * connected. */
	return !gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_ac_present_odl));
}

void board_extpower_enable_interrupt(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_ac_present));
}
