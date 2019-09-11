/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger_mt6370.h"
#include "console.h"
#include "driver/tcpm/mt6370.h"
#include "driver/charger/rt946x.h"
#include "hooks.h"
#include "power.h"
#include "usb_pd.h"
#include "util.h"

#define BAT_LEVEL_PD_LIMIT 85

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

#ifndef CONFIG_BATTERY_SMART
int board_cut_off_battery(void)
{
	/* The cut-off procedure is recommended by Richtek. b/116682788 */
	rt946x_por_reset();
	mt6370_vconn_discharge(0);
	rt946x_cutoff_battery();

	return EC_SUCCESS;
}
#endif

void mt6370_charger_profile_override(struct charge_state_data *curr)
{
	static int previous_chg_limit_mv;
	int chg_limit_mv;

	/* Limit input (=VBUS) to 5V when soc > 85% and charge current < 1A. */
	if (!(curr->batt.flags & BATT_FLAG_BAD_CURRENT) &&
			charge_get_percent() > BAT_LEVEL_PD_LIMIT &&
			curr->batt.current < 1000) {
		chg_limit_mv = 5500;
	} else if (power_get_state() == POWER_S0) {
		/*
		 * b/134227872: limit power to 5V/2A in S0 to prevent
		 * overheat
		 */
		chg_limit_mv = 5500;
	} else {
		chg_limit_mv = PD_MAX_VOLTAGE_MV;
	}

	if (chg_limit_mv != previous_chg_limit_mv)
		CPRINTS("VBUS limited to %dmV", chg_limit_mv);
	previous_chg_limit_mv = chg_limit_mv;

	/* Pull down VBUS */
	if (pd_get_max_voltage() != chg_limit_mv)
		pd_set_external_voltage_limit(0, chg_limit_mv);

	/*
	 * When the charger says it's done charging, even if fuel gauge says
	 * SOC < BATTERY_LEVEL_NEAR_FULL, we'll overwrite SOC with
	 * BATTERY_LEVEL_NEAR_FULL. So we can ensure both Chrome OS UI
	 * and battery LED indicate full charge.
	 */
	if (rt946x_is_charge_done()) {
		curr->batt.state_of_charge = MAX(BATTERY_LEVEL_NEAR_FULL,
						 curr->batt.state_of_charge);
	}

}

static void board_charge_termination(void)
{
	static uint8_t te;
	/* Enable charge termination when we are sure battery is present. */
	if (!te && battery_is_present() == BP_YES) {
		if (!rt946x_enable_charge_termination(1))
			te = 1;
	}
}
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE,
	     board_charge_termination,
	     HOOK_PRIO_DEFAULT);

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	/* b/134227872: Limit input current to 2A in S0 to prevent overheat */
	if (power_get_state() == POWER_S0)
		charge_set_input_current_limit(MIN(charge_ma, 2000), charge_mv);
	else
		charge_set_input_current_limit(
				MAX(charge_ma, CONFIG_CHARGER_INPUT_CURRENT),
				charge_mv);
}
