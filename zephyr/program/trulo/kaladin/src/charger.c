/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger.h"
#include "driver/charger/isl95522.h"
#include "driver/charger/isl95522_public.h"
#include "extpower.h"
#include "hooks.h"
#include "i2c.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(charger_init, LOG_LEVEL_INF);

#define AC_PROCHOT_RATE 1.2

static enum ec_error_list isl95522_write(int chgnum, int offset, int value)
{
	int rv = i2c_write16(chg_chips[chgnum].i2c_port,
			     chg_chips[chgnum].i2c_addr_flags, offset, value);
	if (rv)
		LOG_INF("%s failed (%d)", __func__, rv);

	return rv;
}

static enum ec_error_list isl95522_update(int chgnum, int offset, uint16_t mask,
					  enum mask_update_action action)
{
	int rv = i2c_update16(chg_chips[chgnum].i2c_port,
			      chg_chips[chgnum].i2c_addr_flags, offset, mask,
			      action);
	if (rv)
		LOG_INF("%s failed (%d)", __func__, rv);

	return rv;
}

static void set_ac_prochot(void)
{
	int input_current;

	if (extpower_is_present()) {
		if (!charger_get_input_current_limit(0, &input_current)) {
			input_current = (input_current * AC_PROCHOT_RATE);
			LOG_INF("set_ac_prochot: %d", input_current);
			isl95522_set_ac_prochot(0, input_current);
		}
	} else {
		/* follow ISL95522 data sheet 0x47H default value */
		LOG_INF("set_ac_prochot: default");
		isl95522_set_ac_prochot(0, 6144);
	}
}
DECLARE_HOOK(HOOK_POWER_SUPPLY_CHANGE, set_ac_prochot, HOOK_PRIO_DEFAULT);

static void set_prochot_debounce(void)
{
	int ctl_val, rv;

	ctl_val = ISL95522_REG_PROCHOT_DEBOUNCE_500US;
	rv = isl95522_write(0, ISL95522_REG_PROCHOT_DEBOUNCE, ctl_val);
}

static void set_prochot_duration(void)
{
	int ctl_val, rv;

	ctl_val = ISL95522_REG_PROCHOT_DURATION_10MS;
	rv = isl95522_write(0, ISL95522_REG_PROCHOT_DURATION, ctl_val);
}

static void set_psys_enable(int enable)
{
	/* Set or clear PSYS bit */
	isl95522_update(0, ISL95522_REG_CONTROL1, ISL95522_REG_CONTROL1_PSYS,
			(enable) ? MASK_SET : MASK_CLR);
}

static void set_reg_control_2(void)
{
	/* Clr bit for trickle charge 256mA */
	isl95522_update(0, ISL95522_REG_CONTROL2,
			ISL95522_REG_CONTROL2_TRICKLE_CHARGE, MASK_CLR);
}

static void set_chg_custom_setting(void)
{
	LOG_INF("kaladin: set_chg_reg_custom");

	set_prochot_debounce();
	set_prochot_duration();
	/* Set dc prochot value by kaladin battery design */
	isl95522_set_dc_prochot(0, 4352);
	/* Set ISL95522 data sheet 0x47H default value */
	isl95522_set_ac_prochot(0, 6144);
	set_psys_enable(0);
	set_reg_control_2();
}
DECLARE_HOOK(HOOK_INIT, set_chg_custom_setting,
	     HOOK_PRIO_POST_BATTERY_INIT + 1);

static void charger_power_off(void)
{
	LOG_INF("kaladin: disable_chg_psys");

	set_psys_enable(0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, charger_power_off, HOOK_PRIO_DEFAULT);

static void charger_power_on(void)
{
	LOG_INF("kaladin: enable_chg_psys");

	set_psys_enable(1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, charger_power_on, HOOK_PRIO_DEFAULT);
