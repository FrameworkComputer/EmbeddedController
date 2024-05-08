/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "base_state.h"
#include "charger.h"
#include "chipset.h"
#include "driver/charger/rt9490.h"
#include "extpower.h"
#include "gpio/gpio_int.h"
#include "gpio_signal.h"
#include "hooks.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/init.h>

#include <ap_power/ap_power.h>

#define I2C3_NODE DT_NODELABEL(i2c3)
PINCTRL_DT_DEFINE(I2C3_NODE);

static void board_i2c3_ctrl(bool enable)
{
	if (DEVICE_DT_GET(
		    DT_GPIO_CTLR_BY_IDX(DT_NODELABEL(i2c3), scl_gpios, 0)) ==
	    DEVICE_DT_GET(DT_NODELABEL(gpiof))) {
		const struct pinctrl_dev_config *pcfg =
			PINCTRL_DT_DEV_CONFIG_GET(I2C3_NODE);

		if (enable) {
			pinctrl_apply_state(pcfg, PINCTRL_STATE_DEFAULT);
		} else {
			pinctrl_apply_state(pcfg, PINCTRL_STATE_SLEEP);
		}
	}
}

static void geralt_common_init(void)
{
	gpio_enable_dt_interrupt(
		GPIO_INT_FROM_NODELABEL(int_ap_xhci_init_done));
}
DECLARE_HOOK(HOOK_INIT, geralt_common_init, HOOK_PRIO_PRE_DEFAULT);

static void board_enable_i2c3(void)
{
	board_i2c3_ctrl(1);
}
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, board_enable_i2c3, HOOK_PRIO_FIRST);

static void board_disable_i2c3(void)
{
	board_i2c3_ctrl(0);
}
DECLARE_HOOK(HOOK_CHIPSET_HARD_OFF, board_disable_i2c3, HOOK_PRIO_LAST);

static void base_attached_hook(void)
{
	const struct device *touchpad =
		DEVICE_DT_GET(DT_NODELABEL(hid_i2c_target));
	bool hid_enable = true;

	hid_enable = hid_enable && base_get_state();
	hid_enable = hid_enable &&
		     chipset_in_or_transitioning_to_state(CHIPSET_STATE_ON);

	if (hid_enable) {
		i2c_target_driver_register(touchpad);
	} else {
		i2c_target_driver_unregister(touchpad);
	}
}
DECLARE_HOOK(HOOK_BASE_ATTACHED_CHANGE, base_attached_hook, HOOK_PRIO_DEFAULT);

static void board_suspend_handler(struct ap_power_ev_callback *cb,
				  struct ap_power_ev_data data)
{
	int value;

	switch (data.event) {
	default:
		return;

	case AP_POWER_RESUME:
		value = 1;
		break;

	case AP_POWER_SUSPEND:
		value = 0;
		break;
	}
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_5v_usm), value);
	base_attached_hook();
}

static int install_suspend_handler(void)
{
	static struct ap_power_ev_callback cb;

	/*
	 * Add a callback for suspend/resume.
	 */
	ap_power_ev_init_callback(&cb, board_suspend_handler,
				  AP_POWER_RESUME | AP_POWER_SUSPEND);
	ap_power_ev_add_callback(&cb);
	return 0;
}

SYS_INIT(install_suspend_handler, APPLICATION, 1);

static void board_hook_ac_change(void)
{
	rt9490_enable_adc(CHARGER_SOLO, extpower_is_present());
}
DECLARE_HOOK(HOOK_AC_CHANGE, board_hook_ac_change, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_INIT, board_hook_ac_change, HOOK_PRIO_LAST);
