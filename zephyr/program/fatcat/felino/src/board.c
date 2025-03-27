/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio/gpio_int.h"
#include "gpio_signal.h"

#include <zephyr/drivers/gpio.h>

void edp_bklt_interrupt(enum gpio_signal signal)
{
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_edp_bklt_en))) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_edp_bklt_en), 1);
	} else {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_edp_bklt_en), 0);
	}
}

static int edp_bklt_init(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_edp_bklt));

	return 0;
}
SYS_INIT(edp_bklt_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
