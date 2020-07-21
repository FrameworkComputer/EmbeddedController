/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "charge_state_v2.h"
#include "charger_mt6370.h"
#include "console.h"
#include "driver/charger/rt946x.h"
#include "driver/tcpm/mt6370.h"
#include "hooks.h"
#include "power.h"
#include "timer.h"
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

/* wait time to evaluate charger thermal status */
static timestamp_t thermal_wait_until;
/* input current bound when charger throttled */
static int throttled_ma = PD_MAX_CURRENT_MA;
/* charge_ma in last board_set_charge_limit call */
static int prev_charge_limit;
/* charge_mv in last board_set_charge_limit call */
static int prev_charge_mv;

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

static void board_set_charge_limit_throttle(int charge_ma, int charge_mv)
{
	charge_set_input_current_limit(
		MIN(throttled_ma, MAX(charge_ma, CONFIG_CHARGER_INPUT_CURRENT)),
		charge_mv);
}

static void battery_thermal_control(struct charge_state_data *curr)
{
	int input_current, jc_temp;
	static int skip_reset;
	/*
	 * mt6370's input current setting is 50mA step, use 50 as well for
	 * easy value mapping.
	 */
	const int k_p = 50;

	if (charge_manager_get_charger_voltage() == 5000 ||
	    curr->state != ST_CHARGE) {
		/* We already set the charge limit, do not reset it again. */
		if (skip_reset)
			return;
		skip_reset = 1;
		thermal_wait_until.val = 0;
		throttled_ma = PD_MAX_CURRENT_MA;
		board_set_charge_limit_throttle(prev_charge_limit,
						prev_charge_mv);
		return;
	}

	skip_reset = 0;

	if (thermal_wait_until.val == 0)
		goto thermal_exit;

	if (get_time().val < thermal_wait_until.val)
		return;

	/* If we fail to read adc, skip for this cycle. */
	if (rt946x_get_adc(MT6370_ADC_TEMP_JC, &jc_temp))
		return;

	/* If we fail to read input curr limit, skip for this cycle. */
	if (charger_get_input_current(CHARGER_SOLO, &input_current))
		return;

	/*
	 * If input current limit is maximum, and we are under thermal budget,
	 * just skip.
	 */
	if (input_current == PD_MAX_CURRENT_MA &&
	    jc_temp < thermal_bound.target + thermal_bound.err)
		return;

	/* If the temp is within +- err, thermal is under control */
	if (jc_temp < thermal_bound.target + thermal_bound.err &&
	    jc_temp > thermal_bound.target - thermal_bound.err)
		return;

	/*
	 * PID algorithm (https://en.wikipedia.org/wiki/PID_controller),
	 * and operates on only P value.
	 */
	throttled_ma = MIN(
		PD_MAX_CURRENT_MA,
		/*
		 * Should not pass the previously set input current by
		 * charger manager.  This value might be related the charger's
		 * capability.
		 */
		MIN(prev_charge_limit,
		    input_current + k_p * (thermal_bound.target - jc_temp)));

	/* If the input current doesn't change, just skip. */
	if (throttled_ma != input_current)
		board_set_charge_limit_throttle(throttled_ma, prev_charge_mv);

thermal_exit:
	thermal_wait_until.val = get_time().val + (3 * SECOND);
}

int command_jc(int argc, char **argv)
{
	static int prev_jc_temp;
	int jc_temp;

	if (rt946x_get_adc(MT6370_ADC_TEMP_JC, &jc_temp))
		jc_temp = prev_jc_temp;

	ccprintf("JC Temp: %d\n", jc_temp);
	prev_jc_temp = jc_temp;
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(jc, command_jc, "", "mt6370 junction temp");

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

	if (!charge_is_current_stable())
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

		/*
		 * do not alter current by thermal if we just raising PD
		 * voltage
		 */
		thermal_wait_until.val = get_time().val + (10 * SECOND);
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

#ifdef CONFIG_BATTERY_SMART
static void charge_enable_eoc_and_te(void)
{
	rt946x_enable_charge_eoc(1);
	rt946x_enable_charge_termination(1);
}
DECLARE_DEFERRED(charge_enable_eoc_and_te);
#endif

void mt6370_charger_profile_override(struct charge_state_data *curr)
{
	static int previous_chg_limit_mv;
	int chg_limit_mv = pd_get_max_voltage();

	battery_desired_curr_dynamic(curr);

	battery_thermal_control(curr);

#ifdef CONFIG_BATTERY_SMART
	/*
	 * SMP battery uses HW pre-charge circuit and pre-charge current is
	 * limited to ~50mA. Once the charge current is lower than IEOC level
	 * within CHG_TEDG_EOC, and TE is enabled, the charging power path will
	 * be turned off. Disable EOC and TE when battery stays over discharge
	 * state, otherwise enable EOC and TE.
	 */
	if (!(curr->batt.flags & BATT_FLAG_BAD_VOLTAGE)) {
		const struct battery_info *batt_info = battery_get_info();
		static int normal_charge_lock, over_discharge_lock;

		if (curr->batt.voltage < batt_info->voltage_min) {
			normal_charge_lock = 0;

			if (!over_discharge_lock && curr->state == ST_CHARGE) {
				over_discharge_lock = 1;
				rt946x_enable_charge_eoc(0);
				rt946x_enable_charge_termination(0);
			}
		} else {
			over_discharge_lock = 0;

			if (!normal_charge_lock) {
				normal_charge_lock = 1;
				/*
				 * b/148045048: When the battery is activated
				 * in shutdown mode, the adapter cannot boot
				 * DUT automatically. It's a workaround to
				 * delay 4.5 second to enable charger EOC
				 * and TE function.
				 */
				hook_call_deferred(
						&charge_enable_eoc_and_te_data,
						(4.5 * SECOND));
			}
		}
	}
#endif

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

#ifndef CONFIG_BATTERY_SMART
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
#endif

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	prev_charge_limit = charge_ma;
	prev_charge_mv = charge_mv;
	board_set_charge_limit_throttle(charge_ma, charge_mv);
}
