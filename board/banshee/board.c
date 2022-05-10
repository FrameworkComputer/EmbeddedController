/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "button.h"
#include "charge_ramp.h"
#include "charger.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "driver/als_tcs3400.h"
#include "driver/charger/isl9241.h"
#include "fw_config.h"
#include "hooks.h"
#include "keyboard_customization.h"
#include "lid_switch.h"
#include "power_button.h"
#include "power.h"
#include "registers.h"
#include "switch.h"
#include "throttle_ap.h"
#include "usbc_config.h"

#include "gpio_list.h" /* Must come after other header files. */

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

void board_set_charger_current_limit_deferred(void)
{
	int action;
	int rv;

	if (extpower_is_present() &&
		(battery_get_disconnect_state() != BATTERY_NOT_DISCONNECTED))
		/* AC only or AC+DC but battery is disconnect */
		action = MASK_SET;
	else
		action = MASK_CLR;

	rv = i2c_update16(chg_chips[CHARGER_SOLO].i2c_port,
				chg_chips[CHARGER_SOLO].i2c_addr_flags,
				ISL9241_REG_CONTROL3,
				ISL9241_CONTROL3_INPUT_CURRENT_LIMIT, action);

	if (rv)
		CPRINTF("Could not set charger input current limit! Error: %d\n"
		, rv);
}

DECLARE_DEFERRED(board_set_charger_current_limit_deferred);
DECLARE_HOOK(HOOK_SECOND, board_set_charger_current_limit_deferred,
	HOOK_PRIO_DEFAULT);

void battery_present_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&board_set_charger_current_limit_deferred_data, 0);
}

void board_init(void)
{
	int board_id = get_board_id();

	gpio_enable_interrupt(GPIO_EC_BATT_PRES_ODL);
	hook_call_deferred(&board_set_charger_current_limit_deferred_data, 0);

	if (board_id == 0) {
		/* keyboard_col2_inverted on board id 0 */
		gpio_set_flags(GPIO_EC_KSO_04_INV, GPIO_ODR_HIGH);
		gpio_set_alternate_function(GPIO_PORT_1, BIT(5),
			GPIO_ALT_FUNC_DEFAULT);
	} else {
		/* keyboard_col4_inverted on board id 1 and later */
		gpio_set_flags(GPIO_EC_KSO_02_INV, GPIO_ODR_HIGH);
		gpio_set_alternate_function(GPIO_PORT_1, BIT(7),
			GPIO_ALT_FUNC_DEFAULT);
	}

	board_id_keyboard_col_inverted(board_id);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
