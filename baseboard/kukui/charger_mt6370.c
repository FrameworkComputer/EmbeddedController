/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "charge_state.h"
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

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)

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
		MIN(throttled_ma,
		    MAX(charge_ma, CONFIG_CHARGER_DEFAULT_CURRENT_LIMIT)),
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
	if (charger_get_input_current_limit(CHARGER_SOLO, &input_current))
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

static int command_jc(int argc, const char **argv)
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
	 * SOC < CONFIG_BATT_HOST_FULL_FACTOR, we'll overwrite SOC with
	 * CONFIG_BATT_HOST_FULL_FACTOR. So we can ensure both Chrome OS UI
	 * and battery LED indicate full charge.
	 *
	 * Enable this hack on on-board gauge only (b/142097561)
	 */
	if (IS_ENABLED(CONFIG_BATTERY_MAX17055) && rt946x_is_charge_done()) {
		curr->batt.state_of_charge = MAX(CONFIG_BATT_HOST_FULL_FACTOR,
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
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, board_charge_termination,
	     HOOK_PRIO_DEFAULT);
#endif

__override void board_set_charge_limit(int port, int supplier, int charge_ma,
				       int max_ma, int charge_mv)
{
	prev_charge_limit = charge_ma;
	prev_charge_mv = charge_mv;
	board_set_charge_limit_throttle(charge_ma, charge_mv);
}
