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
#include "peripheral_charger.h"
#include "power.h"
#include "timer.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>

#include <ap_power/ap_power.h>

#define INT_RECHECK_US 5000
/* Periodic check interval for stylus battery status in S3 */
#define PCHG_POLICY_DELAY (1000 * USEC_PER_MSEC)
static bool pchg_low_power_mode = false;

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

static void usm_enable(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_5p0va_pwr_mode), 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, usm_enable, HOOK_PRIO_DEFAULT);

static void usm_disable(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_5p0va_pwr_mode), 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, usm_disable, HOOK_PRIO_DEFAULT);

static void pchg_policy(void);
DECLARE_DEFERRED(pchg_policy);

static void pchg_policy(void)
{
	enum power_state chipset_state = power_get_state();
	if (chipset_state == POWER_S0) {
		if (pchg_low_power_mode == true) {
			pchg_low_power_mode = false;
			ccprints("pchg: resume from low power (S0)");
			pchg_startup();
		}
	} else if (chipset_state == POWER_S3) {
		if (pchg_get_battery_percent(0) >= 100) {
			if (pchg_low_power_mode == false) {
				pchg_low_power_mode = true;
				gpio_pin_set_dt(
					GPIO_DT_FROM_NODELABEL(gpio_ec_pen_dis),
					0);
				ccprints("pchg: enter low power (S3, full)");
			}
		}
	}
	hook_call_deferred(&pchg_policy_data, PCHG_POLICY_DELAY);
}

void board_pchg_power_on(int port, bool on)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_pen_dis), on);
	if (on) {
		pchg_low_power_mode = false;
		hook_call_deferred(&pchg_policy_data, 0);
	} else {
		hook_call_deferred(&pchg_policy_data, -1);
	}
}
