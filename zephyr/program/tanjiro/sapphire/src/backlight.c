/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "gpio/gpio_int.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "timer.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>

#include <ap_power/ap_power.h>

static bool value_en;

static void set_tp_en_pin(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_tchscr_report_en_od),
			value_en);
}
DECLARE_DEFERRED(set_tp_en_pin);

static void set_bl_en_pin(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_en_od), value_en);
}
DECLARE_DEFERRED(set_bl_en_pin);

void ap_bl_en_interrupt(enum gpio_signal signal)
{
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ap_bl_en_odl))) {
		value_en = true;
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_en_od),
				value_en);
		hook_call_deferred(&set_tp_en_pin_data, 3 * USEC_PER_MSEC);
	} else {
		value_en = false;
		gpio_pin_set_dt(
			GPIO_DT_FROM_NODELABEL(gpio_ec_tchscr_report_en_od),
			value_en);
		hook_call_deferred(&set_bl_en_pin_data, 3 * USEC_PER_MSEC);
	}
}

static void ap_bl_en_init(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_ap_bl_en));
}
DECLARE_HOOK(HOOK_INIT, ap_bl_en_init, HOOK_PRIO_DEFAULT);
