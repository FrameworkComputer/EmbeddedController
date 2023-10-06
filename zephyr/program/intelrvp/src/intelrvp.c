/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TODO: b/218904113: Convert to using Zephyr GPIOs */
#include "gpio.h"
#include "hooks.h"

#ifndef CONFIG_PLATFORM_EC_SHARED_SPI_FLASH
static void board_init(void)
{
	/* Enable SOC SPI */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(ec_spi_oe_mecc), 1);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_LAST);
#endif

__override void intel_x86_sys_reset_delay(void)
{
	/*
	 * From MAX6818 Data sheet, Range of 'Debounce Duaration' is
	 * Minimum - 20 ms, Typical - 40 ms, Maximum - 80 ms.
	 * See b/153128296.
	 */
	udelay(60 * MSEC);
}
