/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio/gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "lid_switch.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(touch_en, LOG_LEVEL_INF);

/* touch panel power sequence control */

#define TOUCH_ENABLE_DELAY_MS (500 * USEC_PER_MSEC)
#define TOUCH_DISABLE_DELAY_MS (0 * USEC_PER_MSEC)

void touch_disable(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_touch_en), 0);
}
DECLARE_DEFERRED(touch_disable);

void touch_enable(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_touch_en), 1);
}
DECLARE_DEFERRED(touch_enable);

void soc_edp_bl_interrupt(enum gpio_signal signal)
{
	int state;

	if (signal != GPIO_SIGNAL(DT_NODELABEL(gpio_soc_edp_bl_en)))
		return;

	state = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_soc_edp_bl_en));

	LOG_INF("%s: %d", __func__, state);

	if (state && lid_is_open()) {
		hook_call_deferred(&touch_enable_data, TOUCH_ENABLE_DELAY_MS);
	} else {
		hook_call_deferred(&touch_disable_data, TOUCH_DISABLE_DELAY_MS);
	}
}

static void touch_lid_change(void)
{
	if (!lid_is_open()) {
		hook_call_deferred(&touch_disable_data, TOUCH_DISABLE_DELAY_MS);
	} else {
		if (gpio_pin_get_dt(
			    GPIO_DT_FROM_NODELABEL(gpio_soc_edp_bl_en)) &&
		    !gpio_pin_get_dt(
			    GPIO_DT_FROM_NODELABEL(gpio_ec_touch_en))) {
			hook_call_deferred(&touch_enable_data,
					   TOUCH_ENABLE_DELAY_MS);
		}
	}
}
DECLARE_HOOK(HOOK_LID_CHANGE, touch_lid_change, HOOK_PRIO_DEFAULT);

static void touch_enable_init(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_soc_edp_bl_en));
}
DECLARE_HOOK(HOOK_INIT, touch_enable_init, HOOK_PRIO_DEFAULT);
