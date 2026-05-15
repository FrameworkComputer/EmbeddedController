/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "gpio/gpio_int.h"
#include "gpio_signal.h"
#include "hooks.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>

void edp_bl_interrupt(enum gpio_signal signal)
{
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_edp_bl_en_1v8))) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tchscr_report_ec),
				0);
	} else {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tchscr_report_ec),
				1);
	}
}

static void ap_bl_en_init(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_edp_bl_en));
}
DECLARE_HOOK(HOOK_INIT, ap_bl_en_init, HOOK_PRIO_DEFAULT);
