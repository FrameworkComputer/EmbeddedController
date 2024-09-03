/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charger.h"
#include "driver/charger/bq257x0_regs.h"
#include "extpower.h"
#include "hooks.h"
#include "i2c.h"

#define LOTSO_CHARGER_MIN_INPUT_VOLTAGE 0x240
static void bq25710_min_input_voltage(void)
{
	if (extpower_is_present()) {
		i2c_write16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
			    BQ25710_REG_INPUT_VOLTAGE,
			    LOTSO_CHARGER_MIN_INPUT_VOLTAGE);
	}
}
DECLARE_HOOK(HOOK_INIT, bq25710_min_input_voltage, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_AC_CHANGE, bq25710_min_input_voltage, HOOK_PRIO_DEFAULT);

static void set_bq25710_charge_option(void)
{
	int reg;
	int rv;

	rv = i2c_read16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
			BQ25710_REG_CHARGE_OPTION_0, &reg);
	/* Disable IDPM when AC only */
	if (rv == EC_SUCCESS && !battery_is_present()) {
		reg = SET_BQ_FIELD(BQ257X0, CHARGE_OPTION_0, EN_IDPM, 0, reg);
		i2c_write16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
			    BQ25710_REG_CHARGE_OPTION_0, reg);
	}
}
DECLARE_HOOK(HOOK_INIT, set_bq25710_charge_option, HOOK_PRIO_DEFAULT);
