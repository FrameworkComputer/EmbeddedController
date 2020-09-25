/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* OCPC - One Charger IC Per Type-C module */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "charge_state_v2.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "math_util.h"
#include "ocpc.h"
#include "timer.h"
#include "util.h"

/*
 * These constants were chosen by tuning the PID loop to reduce oscillations and
 * minimize overshoot.
 */
#define KP 1
#define KP_DIV 4
#define KI 1
#define KI_DIV 15
#define KD 1
#define KD_DIV 10

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHARGER, outstr)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)
#define CPRINT_VIZ(format, args...) \
do {							\
	if (viz_output)				\
		cprintf(CC_CHARGER, format, ## args);	\
} while (0)
#define CPRINTS_DBG(format, args...) \
do {							\
	if (debug_output)				\
		cprints(CC_CHARGER, format, ## args);	\
} while (0)

/* Code refactor will be needed if more than 2 charger chips are present */
BUILD_ASSERT(CHARGER_NUM == 2);

static int k_p = KP;
static int k_i = KI;
static int k_d = KD;
static int k_p_div = KP_DIV;
static int k_i_div = KI_DIV;
static int k_d_div = KD_DIV;
static int debug_output;
static int viz_output;

enum phase {
	PHASE_UNKNOWN = -1,
	PHASE_CC,
	PHASE_CV_TRIP,
	PHASE_CV_COMPLETE,
};

__overridable void board_ocpc_init(struct ocpc_data *ocpc)
{
}

enum ec_error_list ocpc_calc_resistances(struct ocpc_data *ocpc,
					 struct batt_params *battery)
{
	int act_chg = ocpc->active_chg_chip;

	/*
	 * In order to actually calculate the resistance, we need to make sure
	 * we're actually charging the battery at a significant rate.
	 */
	if ((battery->current <= 1000) ||
	    (!(ocpc->chg_flags[act_chg] & OCPC_NO_ISYS_MEAS_CAP) &&
	     (ocpc->isys_ma <= 0)) ||
	    (ocpc->vsys_aux_mv < ocpc->vsys_mv)) {
		CPRINTS_DBG("Not charging... won't determine resistance");
		CPRINTS_DBG("vsys_aux_mv: %dmV vsys_mv: %dmV",
			    ocpc->vsys_aux_mv, ocpc->vsys_mv);
		return EC_ERROR_INVALID_CONFIG; /* We must be charging */
	}

	/*
	 * The combined system and battery resistance is the delta between Vsys
	 * and Vbatt divided by Ibatt.
	 */
	if ((ocpc->chg_flags[act_chg] & OCPC_NO_ISYS_MEAS_CAP)) {
		/*
		 * There's no provision to measure Isys, so we cannot separate
		 * out Rsys from Rbatt.
		 */
		ocpc->combined_rsys_rbatt_mo = ((ocpc->vsys_aux_mv -
						 battery->voltage) * 1000) /
						 battery->current;
		CPRINTS_DBG("Rsys+Rbatt: %dmOhm", ocpc->combined_rsys_rbatt_mo);
	} else {
		ocpc->rsys_mo = ((ocpc->vsys_aux_mv - ocpc->vsys_mv) * 1000) /
				 ocpc->isys_ma;
		ocpc->rbatt_mo = ((ocpc->vsys_mv - battery->voltage) * 1000) /
				 battery->current;
		ocpc->combined_rsys_rbatt_mo = ocpc->rsys_mo + ocpc->rbatt_mo;
		CPRINTS_DBG("Rsys: %dmOhm Rbatt: %dmOhm", ocpc->rsys_mo,
			    ocpc->rbatt_mo);
	}
	return EC_SUCCESS;
}

int ocpc_config_secondary_charger(int *desired_input_current,
				  struct ocpc_data *ocpc,
				  int voltage_mv, int current_ma)
{
	int rv = EC_SUCCESS;
	struct batt_params batt;
	const struct battery_info *batt_info;
	struct charger_params charger;
	int vsys_target = 0;
	int drive = 0;
	int i_ma = 0;
	int min_vsys_target;
	int error = 0;
	int derivative = 0;
	static enum phase ph;
	static int prev_limited;
	int chgnum;
	enum ec_error_list result;
	static int iterations;
	int i_step;
	static timestamp_t delay;
	int i, step, loc;

	/*
	 * There's nothing to do if we're not using this charger.  Should
	 * there be more than two charger ICs in the future, the following check
	 * should change to ensure that only the active charger IC is acted
	 * upon.
	 */
	chgnum = charge_get_active_chg_chip();
	if (chgnum != CHARGER_SECONDARY)
		return EC_ERROR_INVAL;

	batt_info = battery_get_info();

	if (current_ma == 0) {
		vsys_target = voltage_mv;
		goto set_vsys;
	}

	/*
	 * Check to see if the charge FET is disabled.  If it's disabled, the
	 * charging loop is broken and increasing VSYS will not actually help.
	 * Therefore, don't make any changes at this time.
	 */
	if (battery_is_charge_fet_disabled() &&
	    (battery_get_disconnect_state() == BATTERY_NOT_DISCONNECTED)) {
		CPRINTS("CFET disabled; not changing VSYS!");

		/*
		 * Let's check back in 5 seconds to see if the CFET is enabled
		 * now.  Note that if this continues to occur, we'll keep
		 * pushing this out.
		 */
		delay = get_time();
		delay.val += (5 * SECOND);
		return EC_ERROR_INVALID_CONFIG;
	}

	/*
	 * The CFET status changed recently, wait until it's no longer disabled
	 * for awhile before modifying VSYS.  This could be the fuel gauge
	 * performing some impedence calculations.
	 */
	if (!timestamp_expired(delay, NULL))
		return EC_ERROR_BUSY;

	result = charger_set_vsys_compensation(chgnum, ocpc, current_ma,
					       voltage_mv);
	switch (result) {
	case EC_SUCCESS:
		/* No further action required, so we're done here. */
		return EC_SUCCESS;

	case EC_ERROR_UNIMPLEMENTED:
		/* Let's get to work */
		break;

	default:
		/* Something went wrong configuring the auxiliary charger IC. */
		CPRINTS("Failed to set VSYS compensation! (%d) (result: %d)",
			chgnum, result);
		return result;
	}

	if (ocpc->last_vsys == OCPC_UNINIT) {
		ph = PHASE_UNKNOWN;
		iterations = 0;
	}


	/*
	 * We need to induce a current flow that matches the requested current
	 * by raising VSYS.  Let's start by getting the latest data that we
	 * know of.
	 */
	batt_info = battery_get_info();
	battery_get_params(&batt);
	ocpc_get_adcs(ocpc);
	charger_get_params(&charger);


	/*
	 * If the system is in S5/G3, we can calculate the board and battery
	 * resistances.
	 */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		/*
		 * In the first few iterations of the loop, charging isn't
		 * stable/correct so making the calculation then leads to some
		 * strange values throwing off the loop even more. However,
		 * after those initial iterations it then begins to behave as
		 * expected. From there onwards, the resistance values aren't
		 * changing _too_ rapidly.  This is why we calculate with every
		 * modulo 4 interval.
		 */
		iterations++;
		if (!(iterations % 4))
			ocpc_calc_resistances(ocpc, &batt);
		iterations %= 5;
	}

	/* Set our current target accordingly. */
	if (batt.desired_voltage) {
		if (batt.voltage < batt.desired_voltage) {
			if (ph < PHASE_CV_TRIP)
				ph = PHASE_CC;
			i_ma = batt.desired_current;
		} else{
			/*
			 * Once the battery voltage reaches the desired voltage,
			 * we should note that we've reached the CV step and set
			 * VSYS to the desired CV + offset.
			 */
			i_ma = batt.current;
			ph = ph == PHASE_CC ? PHASE_CV_TRIP : PHASE_CV_COMPLETE;

		}
	}

	/* Ensure our target is not negative. */
	i_ma = MAX(i_ma, 0);

	/* Convert desired mA to what the charger could actually regulate to. */
	i_step = (int)charger_get_info()->current_step;
	i_ma = (i_ma / i_step) * i_step;

	/*
	 * We'll use our current target and our combined Rsys+Rbatt to seed our
	 * VSYS target.  However, we'll use a PID loop to correct the error and
	 * help drive VSYS to what it _should_ be in order to reach our current
	 * target.  The first time through this function, we won't make any
	 * corrections in order to determine our initial error.
	 */
	if (ocpc->last_vsys != OCPC_UNINIT) {
		error = i_ma - batt.current;
		/* Add some hysteresis. */
		if (ABS(error) < i_step)
			error = 0;

		/* Make a note if we're significantly over target. */
		if (error < -100)
			CPRINTS("OCPC: over target %dmA", error * -1);

		derivative = error - ocpc->last_error;
		ocpc->last_error = error;
		ocpc->integral +=  error;
		if (ocpc->integral > 500)
			ocpc->integral = 500;
	}

	CPRINTS_DBG("phase = %d", ph);
	CPRINTS_DBG("error = %dmA", error);
	CPRINTS_DBG("derivative = %d", derivative);
	CPRINTS_DBG("integral = %d", ocpc->integral);
	CPRINTS_DBG("batt.voltage = %dmV", batt.voltage);
	CPRINTS_DBG("batt.desired_voltage = %dmV", batt.desired_voltage);
	CPRINTS_DBG("batt.desired_current = %dmA", batt.desired_current);
	CPRINTS_DBG("batt.current = %dmA", batt.current);
	CPRINTS_DBG("i_ma = %dmA", i_ma);

	min_vsys_target = MIN(batt.voltage, batt.desired_voltage);
	CPRINTS_DBG("min_vsys_target = %d", min_vsys_target);

	/* Obtain the drive from our PID controller. */
	if (ocpc->last_vsys != OCPC_UNINIT) {
		drive = (k_p * error / k_p_div) +
			(k_i * ocpc->integral / k_i_div) +
			(k_d * derivative / k_d_div);
		/*
		 * Let's limit upward transitions to 200mV.  It's okay to reduce
		 * VSYS rather quickly, but we'll be conservative on
		 * increasing VSYS.
		 */
		if (drive > 200)
			drive = 200;
		CPRINTS_DBG("drive = %d", drive);
	}

	/*
	 * Adjust our VSYS target by applying the calculated drive.  Note that
	 * we won't apply our drive the first time through this function such
	 * that we can determine our initial error.
	 */
	if (ocpc->last_vsys != OCPC_UNINIT)
		vsys_target = ocpc->last_vsys + drive;

	/*
	 * Once we're in the CV region, all we need to do is keep VSYS at the
	 * desired voltage.
	 */
	if (ph >= PHASE_CV_TRIP)
		vsys_target = batt.desired_voltage +
				((batt_info->precharge_current *
				  ocpc->combined_rsys_rbatt_mo) / 1000);

	/*
	 * Ensure VSYS is no higher than the specified maximum battery voltage
	 * plus the voltage drop across the system.
	 */
	vsys_target = CLAMP(vsys_target, min_vsys_target,
			    batt_info->voltage_max +
			    (i_ma * ocpc->combined_rsys_rbatt_mo / 1000));

	/* If we're input current limited, we cannot increase VSYS any more. */
	CPRINTS_DBG("OCPC: Inst. Input Current: %dmA (Limit: %dmA)",
		    ocpc->secondary_ibus_ma, *desired_input_current);
	if ((ocpc->secondary_ibus_ma >= (*desired_input_current * 95 / 100)) &&
	    (vsys_target > ocpc->last_vsys) &&
	    (ocpc->last_vsys != OCPC_UNINIT)) {
		if (!prev_limited)
			CPRINTS("Input limited! Not increasing VSYS");
		prev_limited = 1;
		return rv;
	}
	prev_limited = 0;

set_vsys:
	/* VSYS should never be below the battery's min voltage. */
	vsys_target = MAX(vsys_target, batt_info->voltage_min);
	/* To reduce spam, only print when we change VSYS significantly. */
	if ((ABS(vsys_target - ocpc->last_vsys) > 10) || debug_output)
		CPRINTS("OCPC: Target VSYS: %dmV", vsys_target);
	charger_set_voltage(CHARGER_SECONDARY, vsys_target);
	ocpc->last_vsys = vsys_target;

	/*
	 * Print a visualization graph of the actual current vs. the target.
	 * Each position represents 5% of the target current.
	 */
	if (i_ma != 0) {
		step = 5 * i_ma / 100;
		loc = error / step;
		loc = CLAMP(loc, -10, 10);
		CPRINT_VIZ("[");
		for (i = -10; i <= 10; i++) {
			if (i == 0)
				CPRINT_VIZ(loc == 0 ? "#" : "|");
			else
				CPRINT_VIZ(i == loc ? "o" : "-");
		}
		CPRINT_VIZ("] (actual)%dmA (desired)%dmA\n", batt.current,
			   batt.desired_current);
	}

	return rv;
}

void ocpc_get_adcs(struct ocpc_data *ocpc)
{
	int val;

	val = 0;
	if (!charger_get_vbus_voltage(CHARGER_PRIMARY, &val))
		ocpc->primary_vbus_mv = val;

	val = 0;
	if (!charger_get_input_current(CHARGER_PRIMARY, &val))
		ocpc->primary_ibus_ma = val;

	val = 0;
	if (!charger_get_voltage(CHARGER_PRIMARY, &val))
		ocpc->vsys_mv = val;

	if (board_get_charger_chip_count() <= CHARGER_SECONDARY) {
		ocpc->secondary_vbus_mv = 0;
		ocpc->secondary_ibus_ma = 0;
		ocpc->vsys_aux_mv = 0;
		ocpc->isys_ma = 0;
		return;
	}

	val = 0;
	if (!charger_get_vbus_voltage(CHARGER_SECONDARY, &val))
		ocpc->secondary_vbus_mv = val;

	val = 0;
	if (!charger_get_input_current(CHARGER_SECONDARY, &val))
		ocpc->secondary_ibus_ma = val;

	val = 0;
	if (!charger_get_voltage(CHARGER_SECONDARY, &val))
		ocpc->vsys_aux_mv = val;

	val = 0;
	if (!charger_get_current(CHARGER_SECONDARY, &val))
		ocpc->isys_ma = val;
}

__overridable void ocpc_get_pid_constants(int *kp, int *kp_div,
					  int *ki, int *ki_div,
					  int *kd, int *kd_div)
{
}

static void ocpc_set_pid_constants(void)
{
	ocpc_get_pid_constants(&k_p, &k_p_div, &k_i, &k_i_div, &k_d, &k_d_div);
}
DECLARE_HOOK(HOOK_INIT, ocpc_set_pid_constants, HOOK_PRIO_DEFAULT);

void ocpc_init(struct ocpc_data *ocpc)
{
	/*
	 * We can start off assuming that the board resistance is 0 ohms
	 * and later on, we can update this value if we charge the
	 * system in suspend or off.
	 */
	ocpc->combined_rsys_rbatt_mo = CONFIG_OCPC_DEF_RBATT_MOHMS;
	ocpc->rbatt_mo = CONFIG_OCPC_DEF_RBATT_MOHMS;

	board_ocpc_init(ocpc);
}

static int command_ocpcdebug(int argc, char **argv)
{
	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	if (!strncmp(argv[1], "ena", 3)) {
		debug_output = true;
		viz_output = false;
	} else if (!strncmp(argv[1], "dis", 3)) {
		debug_output = false;
		viz_output = false;
	} else if (!strncmp(argv[1], "viz", 3)) {
		debug_output = false;
		viz_output = true;
	} else if (!strncmp(argv[1], "all", 3)) {
		debug_output = true;
		viz_output = true;
	} else {
		return EC_ERROR_PARAM1;
	}

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(ocpcdebug, command_ocpcdebug,
			     "<enable/viz/all/disable",
			     "Enable/disable debug prints for OCPC data. "
			     "Enable turns on text debug, viz shows a graph."
			     "Each segment is 5% of current target. All shows"
			     " both. Disable shows no debug output.");

static int command_ocpcpid(int argc, char **argv)
{
	int *num, *denom;

	if (argc == 4) {
		switch (argv[1][0]) {
		case 'p':
			num = &k_p;
			denom = &k_p_div;
			break;

		case 'i':
			num = &k_i;
			denom = &k_i_div;
			break;

		case 'd':
			num = &k_d;
			denom = &k_d_div;
			break;
		default:
			return EC_ERROR_PARAM1;
		}

		*num = atoi(argv[2]);
		*denom = atoi(argv[3]);
	}

	/* Print the current constants */
	ccprintf("Kp = %d / %d\n", k_p, k_p_div);
	ccprintf("Ki = %d / %d\n", k_i, k_i_div);
	ccprintf("Kd = %d / %d\n", k_d, k_d_div);
	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(ocpcpid, command_ocpcpid,
			     "[<k/p/d> <numerator> <denominator>]",
			     "Show/Set PID constants for OCPC PID loop");
