/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "driver/charger/rt9490.h"
#include "extpower.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "timer.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>

#include <ap_power/ap_power.h>

#define INT_RECHECK_US 5000

static void board_backlight_handler(struct ap_power_ev_callback *cb,
				    struct ap_power_ev_data data)
{
	switch (data.event) {
	default:
		return;

	case AP_POWER_STARTUP:
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_blpwr),
				1);
		break;

	case AP_POWER_HARD_OFF:
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_blpwr),
				0);
		break;
	}
}

static void board_suspend_handler(struct ap_power_ev_callback *cb,
				  struct ap_power_ev_data data)
{
	const struct device *touchpad =
		DEVICE_DT_GET(DT_NODELABEL(hid_i2c_target));

	switch (data.event) {
	default:
		return;

	case AP_POWER_RESUME:
		i2c_target_driver_register(touchpad);
		break;

	case AP_POWER_SUSPEND:
		i2c_target_driver_unregister(touchpad);
		break;
	}
}

static int install_backlight_handler(void)
{
	static struct ap_power_ev_callback cb;
	static struct ap_power_ev_callback tp;
	/*
	 * Add a callback for start/hardoff to
	 * control the backlight load switch.
	 */
	ap_power_ev_init_callback(&cb, board_backlight_handler,
				  AP_POWER_STARTUP | AP_POWER_HARD_OFF);
	ap_power_ev_init_callback(&tp, board_suspend_handler,
				  AP_POWER_RESUME | AP_POWER_SUSPEND);
	ap_power_ev_add_callback(&cb);
	ap_power_ev_add_callback(&tp);

	return 0;
}

SYS_INIT(install_backlight_handler, APPLICATION, 1);

__overridable void board_rt9490_adc_control(void)
{
	rt9490_enable_adc(CHARGER_SOLO, extpower_is_present());
}

static void board_hook_ac_change(void)
{
	board_rt9490_adc_control();
}
DECLARE_HOOK(HOOK_AC_CHANGE, board_hook_ac_change, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_INIT, board_hook_ac_change, HOOK_PRIO_LAST);

static void check_audio_jack(void)
{
	if (chipset_in_or_transitioning_to_state(CHIPSET_STATE_ON)) {
		if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_jd1)))
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(gpio_5p0va_pwr_mode), 0);
		else
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(gpio_5p0va_pwr_mode), 1);
	} else {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_5p0va_pwr_mode), 0);
	}
}
DECLARE_DEFERRED(check_audio_jack);

DECLARE_HOOK(HOOK_INIT, check_audio_jack, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, check_audio_jack, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, check_audio_jack, HOOK_PRIO_DEFAULT);

void audio_jack_interrupt(enum gpio_signal s)
{
	hook_call_deferred(&check_audio_jack_data, INT_RECHECK_US);
}

static void board_setup_init()
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_jd1));
}
DECLARE_HOOK(HOOK_INIT, board_setup_init, HOOK_PRIO_PRE_DEFAULT);
