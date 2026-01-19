/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger.h"
#include "gpio.h"
#include "hooks.h"
#include "lid_switch.h"

static void set_chg_reg_custom(void)
{
	charger_set_frequency(808);
}
DECLARE_HOOK(HOOK_INIT, set_chg_reg_custom, HOOK_PRIO_POST_BATTERY_INIT + 1);

static void tp_enable(void)
{
	if (lid_is_open()) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tp_disable), true);
	} else {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tp_disable), false);
	}
}
DECLARE_HOOK(HOOK_LID_CHANGE, tp_enable, HOOK_PRIO_DEFAULT);
