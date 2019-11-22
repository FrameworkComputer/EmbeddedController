/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "charger_mt6370.h"
#include "console.h"
#include "driver/tcpm/mt6370.h"
#include "driver/charger/rt946x.h"
#include "hooks.h"
#include "power.h"
#include "usb_common.h"
#include "usb_pd.h"
#include "util.h"

#define BAT_LEVEL_PD_LIMIT 85
#define SYSTEM_PLT_MW 3500
/*
 * b/143318064: Prefer a voltage above 5V to force it picks a voltage
 * above 5V at first. If PREFER_MV is 5V, when desired power is around
 * 15W ~ 11W, it would pick 5V/3A initially, and mt6370 can only sink
 * around 10W, and cause a low charging efficiency.
 */
#define PREVENT_CURRENT_DROP_MV 6000
#define DEFAULT_PREFER_MV 5000
/*
 * We empirically chose 300mA as the limit for when buck inefficiency is
 * noticeable.
 */
#define STABLE_CURRENT_DELTA 300

struct pd_pref_config_t pd_pref_config = {
	.mv = PREVENT_CURRENT_DROP_MV,
	.cv = 70,
	.plt_mw = SYSTEM_PLT_MW,
	.type = PD_PREFER_BUCK,
};

static void update_plt_suspend(void)
{
	pd_pref_config.plt_mw = 0;
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, update_plt_suspend, HOOK_PRIO_DEFAULT);

static void update_plt_resume(void)
{
	pd_pref_config.plt_mw = SYSTEM_PLT_MW;
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, update_plt_resume, HOOK_PRIO_DEFAULT);

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

/*
 * b/143318064: A workwround for mt6370 bad buck efficiency.
 * If the delta of VBUS and VBAT(on krane, desired voltage 4.4V) is too small
 * (i.e. < 500mV), the buck throughput will be bounded, and causing that we
 * can't drain 5V/3A when battery SoC above around 40%.
 * This function watches battery current. If we see battery current drops after
 * switching from high voltage to 5V (This will happen if we enable
 * CONFIG_USB_PD_PREFER_MV and set prefer votage to 5V), the charger will lost
 * power due to the inefficiency (e.g. switch from 9V/1.67A = 15W to 5V/3A,
 * but mt6370 would only sink less than 5V/2.4A = 12W), and we will request a
 * higher voltage PDO to prevent a slow charging time.
 */
static void battery_desired_curr_dynamic(struct charge_state_data *curr)
{
	static int prev_stable_current = CHARGE_CURRENT_UNINITIALIZED;
	static int prev_supply_voltage;
	int supply_voltage;
	int stable_current;
	int delta_current;

	if (curr->state != ST_CHARGE) {
		prev_supply_voltage = 0;
		prev_stable_current = CHARGE_CURRENT_UNINITIALIZED;
		/*
		 * Always force higher voltage on first PD negotiation.
		 * When desired power is around 15W ~ 11W, PD would pick
		 * 5V/3A initially, but mt6370 can't drain that much, and
		 * causes a low charging efficiency.
		 */
		pd_pref_config.mv = PREVENT_CURRENT_DROP_MV;
		return;
	}

	supply_voltage = charge_manager_get_charger_voltage();
	stable_current = charge_get_stable_current();

	if (stable_current == CHARGE_CURRENT_UNINITIALIZED)
		return;

	if (!prev_supply_voltage)
		goto update_charge;

	delta_current = prev_stable_current - stable_current;
	if (curr->batt.state_of_charge >= pd_pref_config.cv &&
	    supply_voltage == DEFAULT_PREFER_MV &&
	    prev_supply_voltage > supply_voltage &&
	    delta_current > STABLE_CURRENT_DELTA) {
		/* Raise perfer voltage above 5000mV */
		pd_pref_config.mv = PREVENT_CURRENT_DROP_MV;
		/*
		 * Delay stable current evaluation for 5 mins if we see a
		 * current drop.  It's a reasonable waiting time since that
		 * the battery desired current can't catch the gap that fast
		 * in the period.
		 */
		charge_reset_stable_current_us(5 * MINUTE);
		/* Rewrite the stable current to re-evalute desired watt */
		charge_set_stable_current(prev_stable_current);
	} else {
		pd_pref_config.mv = DEFAULT_PREFER_MV;
		/*
		 * If the power supply is plugged while battery full,
		 * the stable_current will always be 0 such that we are unable
		 * to switch to 5V. We force evaluating PDO to switch to 5V.
		 */
		if (prev_supply_voltage == supply_voltage && !stable_current &&
		    !prev_stable_current &&
		    supply_voltage != DEFAULT_PREFER_MV &&
		    charge_manager_get_supplier() == CHARGE_SUPPLIER_PD)
			pd_set_new_power_request(
				charge_manager_get_active_charge_port());
	}

update_charge:
	prev_supply_voltage = supply_voltage;
	prev_stable_current = stable_current;
}

void mt6370_charger_profile_override(struct charge_state_data *curr)
{
	static int previous_chg_limit_mv;
	int chg_limit_mv = pd_get_max_voltage();

	battery_desired_curr_dynamic(curr);

	/* Limit input (=VBUS) to 5V when soc > 85% and charge current < 1A. */
	if (!(curr->batt.flags & BATT_FLAG_BAD_CURRENT) &&
	    charge_get_percent() > BAT_LEVEL_PD_LIMIT &&
	    curr->batt.current < 1000 && power_get_state() != POWER_S0)
		chg_limit_mv = 5500;
	else
		chg_limit_mv = PD_MAX_VOLTAGE_MV;

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
	 *
	 * Enable this hack on on-board gauge only (b/142097561)
	 */
	if (IS_ENABLED(CONFIG_BATTERY_MAX17055) && rt946x_is_charge_done()) {
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
	charge_set_input_current_limit(
		MAX(charge_ma, CONFIG_CHARGER_INPUT_CURRENT), charge_mv);
}
