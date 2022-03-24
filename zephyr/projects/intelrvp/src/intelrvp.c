/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TODO: b/218904113: Convert to using Zephyr GPIOs */
#include "gpio.h"
#include "hooks.h"

static int board_init(const struct device *unused)
{
	ARG_UNUSED(unused);
	/* Enable SOC SPI */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(ec_spi_oe_mecc), 1);

	return 0;
}
SYS_INIT(board_init, APPLICATION, HOOK_PRIO_LAST);
