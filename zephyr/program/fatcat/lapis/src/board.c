/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger.h"
#include "cros_board_info.h"
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

static void disable_sleep_bid(void)
{
	uint32_t board_id = 0;
	/* Errors will count as board_id 0 */
	cbi_get_board_version(&board_id);

	if (board_id > 1)
		enable_sleep(SLEEP_MASK_FORCE_NO_DSLEEP);
	else
		disable_sleep(SLEEP_MASK_FORCE_NO_DSLEEP);
}
DECLARE_HOOK(HOOK_INIT, disable_sleep_bid, HOOK_PRIO_POST_I2C);
