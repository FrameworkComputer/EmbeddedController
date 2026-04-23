/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "timer.h"

#include <zephyr/drivers/gpio.h>

static int level;

static void en_tchscr_deferred(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_tchscr_en_3v3), level);
}
DECLARE_DEFERRED(en_tchscr_deferred);

void touch_screen_rst_interrupt(enum gpio_signal signal)
{
	level = gpio_get_level(signal);
	if (level == 1)
		hook_call_deferred(&en_tchscr_deferred_data,
				   150 * USEC_PER_MSEC);
	else
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_tchscr_en_3v3),
				level);
}

static void touch_screen_init(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_tchscr_rst));
}
DECLARE_HOOK(HOOK_INIT, touch_screen_init, HOOK_PRIO_PRE_DEFAULT);
