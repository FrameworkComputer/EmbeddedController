/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* OCPC - One Charger IC Per Type-C module */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "math_util.h"
#include "ocpc.h"
#include "timer.h"
#include "usb_pd.h"
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
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)
#define CPRINT_VIZ(format, args...)                          \
	do {                                                 \
		if (viz_output)                              \
			cprintf(CC_CHARGER, format, ##args); \
	} while (0)
#define CPRINTS_DBG(format, args...)                         \
	do {                                                 \
		if (debug_output)                            \
			cprints(CC_CHARGER, format, ##args); \
	} while (0)
#define CPRINTF_DBG(format, args...)                         \
	do {                                                 \
		if (debug_output)                            \
			cprintf(CC_CHARGER, format, ##args); \
	} while (0)

/* Code refactor will be needed if more than 2 charger chips are present */
BUILD_ASSERT(CHARGER_NUM == 2);

static int k_p = KP;
static int k_i = KI;
static int k_d = KD;
static int k_p_div = KP_DIV;
static int k_i_div = KI_DIV;
static int k_d_div = KD_DIV;
static int drive_limit = CONFIG_OCPC_DEF_DRIVELIMIT_MILLIVOLTS;
static int debug_output;
static int viz_output;

#define NUM_RESISTANCE_SAMPLES 8
#define COMBINED_IDX 0
#define RBATT_IDX 1
#define RSYS_IDX 2
static int resistance_tbl[NUM_RESISTANCE_SAMPLES][3] = {
	/* Rsys+Rbatt                   Rbatt                    Rsys */
	{ CONFIG_OCPC_DEF_RBATT_MOHMS, CONFIG_OCPC_DEF_RBATT_MOHMS, 0 },
	{ CONFIG_OCPC_DEF_RBATT_MOHMS, CONFIG_OCPC_DEF_RBATT_MOHMS, 0 },
	{ CONFIG_OCPC_DEF_RBATT_MOHMS, CONFIG_OCPC_DEF_RBATT_MOHMS, 0 },
	{ CONFIG_OCPC_DEF_RBATT_MOHMS, CONFIG_OCPC_DEF_RBATT_MOHMS, 0 },
	{ CONFIG_OCPC_DEF_RBATT_MOHMS, CONFIG_OCPC_DEF_RBATT_MOHMS, 0 },
	{ CONFIG_OCPC_DEF_RBATT_MOHMS, CONFIG_OCPC_DEF_RBATT_MOHMS, 0 },
	{ CONFIG_OCPC_DEF_RBATT_MOHMS, CONFIG_OCPC_DEF_RBATT_MOHMS, 0 },
	{ CONFIG_OCPC_DEF_RBATT_MOHMS, CONFIG_OCPC_DEF_RBATT_MOHMS, 0 },
};
static int resistance_tbl_idx;
static int mean_resistance[3];
static int stddev_resistance[3];
static int ub[3];
static int lb[3];

enum phase {
	PHASE_UNKNOWN = -1,
	PHASE_PRECHARGE,
	PHASE_CC,
	PHASE_CV_TRIP,
	PHASE_CV_COMPLETE,
};

__overridable void board_ocpc_init(struct ocpc_data *ocpc)
{
}

static enum ec_error_list ocpc_precharge_enable(bool enable);

static void calc_resistance_stats(struct ocpc_data *ocpc)
{
	int i;
	int j;
	int sum;
	int cols = 3;
	int act_chg = ocpc->active_chg_chip;

	/* Only perform separate stats on Rsys and Rbatt if necessary. */
	if ((ocpc->chg_flags[act_chg] & OCPC_NO_ISYS_MEAS_CAP))
		cols = 1;

	/* Calculate mean */
	for (i = 0; i < cols; i++) {
		sum = 0;
		for (j = 0; j < NUM_RESISTANCE_SAMPLES; j++) {
			sum += resistance_tbl[j][i];
			CPRINTF_DBG("%d ", resistance_tbl[j][i]);
		}
		CPRINTF_DBG("\n");

		mean_resistance[i] = sum / NUM_RESISTANCE_SAMPLES;

		/* Calculate standard deviation */
		sum = 0;
		for (j = 0; j < NUM_RESISTANCE_SAMPLES; j++)
			sum += POW2(resistance_tbl[j][i] - mean_resistance[i]);

		stddev_resistance[i] =
			fp_sqrtf(INT_TO_FP(sum / NUM_RESISTANCE_SAMPLES));
		stddev_resistance[i] = FP_TO_INT(stddev_resistance[i]);
		/*
		 * Don't let our stddev collapse to 0 to continually consider
		 * new values.
		 */
		stddev_resistance[i] = MAX(stddev_resistance[i], 1);
		CPRINTS_DBG("%d: mean: %d stddev: %d", i, mean_resistance[i],
			    stddev_resistance[i]);
		lb[i] = MAX(0, mean_resistance[i] - (3 * stddev_resistance[i]));
		ub[i] = mean_resistance[i] + (3 * stddev_resistance[i]);
	}
}

static bool is_within_range(struct ocpc_data *ocpc, int combined, int rbatt,
			    int rsys)
{
	int act_chg = ocpc->active_chg_chip;
	bool valid;

	/* Discard measurements not within a 6 std. dev. window. */
	if ((ocpc->chg_flags[act_chg] & OCPC_NO_ISYS_MEAS_CAP)) {
		/* We only know the combined Rsys+Rbatt */
		valid = (combined > 0) && (combined <= ub[COMBINED_IDX]) &&
			(combined >= lb[COMBINED_IDX]);
	} else {
		valid = (rsys <= ub[RSYS_IDX]) && (rsys >= lb[RSYS_IDX]) &&
			(rbatt <= ub[RBATT_IDX]) && (rbatt >= lb[RBATT_IDX]) &&
			(rsys > 0) && (rbatt > 0);
	}

	if (!valid)
		CPRINTS_DBG("Discard Rc:%d Rb:%d Rs:%d", combined, rbatt, rsys);

	return valid;
}

enum ec_error_list ocpc_calc_resistances(struct ocpc_data *ocpc,
					 struct batt_params *battery)
{
	int act_chg;
	static bool seeded;
	static int initial_samples;
	int combined;
	int rsys = -1;
	int rbatt = -1;

#ifdef TEST_BUILD
	/* Mechanism for resetting static vars in tests */
	if (!ocpc && !battery) {
		seeded = false;
		initial_samples = 0;

		return EC_SUCCESS;
	}
#endif

	act_chg = ocpc->active_chg_chip;

	/*
	 * In order to actually calculate the resistance, we need to make sure
	 * we're actually charging the battery at a significant rate.  The LSB
	 * of a charger IC can be as high as 96mV.  Assuming a resistance of 60
	 * mOhms, we would need a current of 1666mA to have a voltage delta of
	 * 100mV.
	 */
	if ((battery->current <= 1666) ||
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
		combined = ((ocpc->vsys_aux_mv - battery->voltage) * 1000) /
			   battery->current;
	} else {
		rsys = ((ocpc->vsys_aux_mv - ocpc->vsys_mv) * 1000) /
		       ocpc->isys_ma;
		rbatt = ((ocpc->vsys_mv - battery->voltage) * 1000) /
			battery->current;
		combined = rsys + rbatt;
	}

	/* Discard measurements not within a 6 std dev window. */
	if ((!seeded) ||
	    (seeded && is_within_range(ocpc, combined, rbatt, rsys))) {
		if (!(ocpc->chg_flags[act_chg] & OCPC_NO_ISYS_MEAS_CAP)) {
			resistance_tbl[resistance_tbl_idx][RSYS_IDX] =
				MAX(rsys, 0);
			resistance_tbl[resistance_tbl_idx][RBATT_IDX] =
				MAX(rbatt, CONFIG_OCPC_DEF_RBATT_MOHMS);
		}
		resistance_tbl[resistance_tbl_idx][COMBINED_IDX] =
			MAX(combined, CONFIG_OCPC_DEF_RBATT_MOHMS);
		calc_resistance_stats(ocpc);
		resistance_tbl_idx =
			(resistance_tbl_idx + 1) % NUM_RESISTANCE_SAMPLES;
	}

	if (seeded) {
		ocpc->combined_rsys_rbatt_mo =
			MAX(mean_resistance[COMBINED_IDX],
			    CONFIG_OCPC_DEF_RBATT_MOHMS);

		if (!(ocpc->chg_flags[act_chg] & OCPC_NO_ISYS_MEAS_CAP)) {
			ocpc->rsys_mo = mean_resistance[RSYS_IDX];
			ocpc->rbatt_mo = MAX(mean_resistance[RBATT_IDX],
					     CONFIG_OCPC_DEF_RBATT_MOHMS);
			CPRINTS_DBG("Rsys: %dmOhm Rbatt: %dmOhm", ocpc->rsys_mo,
				    ocpc->rbatt_mo);
		}

		CPRINTS_DBG("Rsys+Rbatt: %dmOhm", ocpc->combined_rsys_rbatt_mo);
	} else {
		seeded = ++initial_samples >= (2 * NUM_RESISTANCE_SAMPLES) ?
				 true :
				 false;
	}

	return EC_SUCCESS;
}

int ocpc_config_secondary_charger(int *desired_charger_input_current,
				  struct ocpc_data *ocpc,
				  int desired_batt_voltage_mv,
				  int desired_batt_current_ma)
{
	int rv = EC_SUCCESS;
	struct batt_params batt;
	const struct battery_info *batt_info;
	struct charger_params charger;
	int vsys_target = 0;
	int drive = 0;
	int i_ma = 0;
	static int i_ma_CC_CV;
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
	int i, loc;
	bool icl_reached = false;
	static timestamp_t precharge_exit;

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

	if (desired_batt_current_ma == 0) {
		vsys_target = desired_batt_voltage_mv;
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

	result = charger_set_vsys_compensation(
		chgnum, ocpc, desired_batt_current_ma, desired_batt_voltage_mv);
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
		precharge_exit.val = 0;
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
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF |
			     CHIPSET_STATE_ANY_SUSPEND)) {
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
		if (((batt.voltage < batt_info->voltage_min) ||
		     ((batt.voltage < batt_info->voltage_normal) &&
		      (desired_batt_current_ma >= 0) &&
		      (desired_batt_current_ma <=
		       batt_info->precharge_current))) &&
		    (ph != PHASE_PRECHARGE)) {
			/*
			 * If the charger IC doesn't support the linear charge
			 * feature, proceed to the CC phase.
			 */
			result = ocpc_precharge_enable(true);
			if (result == EC_ERROR_UNIMPLEMENTED) {
				ph = PHASE_CC;
			} else if (result == EC_SUCCESS) {
				CPRINTS("OCPC: Enabling linear precharge");
				ph = PHASE_PRECHARGE;
				i_ma = desired_batt_current_ma;
			}
		} else if (batt.voltage < batt.desired_voltage) {
			if ((ph == PHASE_PRECHARGE) &&
			    (desired_batt_current_ma >
			     batt_info->precharge_current)) {
				/*
				 * Precharge phase is complete.  Now set the
				 * target VSYS to the battery voltage to prevent
				 * a large current spike during the transition.
				 */
				/*
				 * If we'd like to exit precharge, let's wait a
				 * short delay.
				 */
				if (!precharge_exit.val) {
					CPRINTS("OCPC: Preparing to exit "
						"precharge");
					precharge_exit = get_time();
					precharge_exit.val += 3 * SECOND;
				}
				if (timestamp_expired(precharge_exit, NULL)) {
					CPRINTS("OCPC: Precharge complete");
					charger_set_voltage(CHARGER_SECONDARY,
							    batt.voltage);
					ocpc->last_vsys = batt.voltage;
					ocpc_precharge_enable(false);
					ph = PHASE_CC;
					precharge_exit.val = 0;
				}
			}

			if ((ph != PHASE_PRECHARGE) && (ph < PHASE_CV_TRIP))
				ph = PHASE_CC;
			i_ma = desired_batt_current_ma;
		} else {
			/*
			 * Once the battery voltage reaches the desired voltage,
			 * we should note that we've reached the CV step and set
			 * VSYS to the desired CV + offset.
			 */
			i_ma = batt.current;
			ph = ph == PHASE_CC ? PHASE_CV_TRIP : PHASE_CV_COMPLETE;
			if (ph == PHASE_CV_TRIP)
				i_ma_CC_CV = batt.current;
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

		/* Uses charger input error if controller is proportional only.
		 */
		if ((k_i == 0) && (k_d == 0)) {
			int charger_input_error =
				(*desired_charger_input_current -
				 ocpc->secondary_ibus_ma);
			error = MIN(error, charger_input_error);
		}

		/* Add some hysteresis. */
		if (ABS(error) < (i_step / 2))
			error = 0;

		/* Make a note if we're significantly over target. */
		if (error < -100)
			CPRINTS("OCPC: over target %dmA", error * -1);

		derivative = error - ocpc->last_error;
		ocpc->last_error = error;
		ocpc->integral += error;
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
	if ((ocpc->last_vsys != OCPC_UNINIT) && (ph > PHASE_PRECHARGE)) {
		drive = (k_p * error / k_p_div) +
			(k_i * ocpc->integral / k_i_div) +
			(k_d * derivative / k_d_div);
		/*
		 * Let's limit upward transitions to 10mV.  It's okay to reduce
		 * VSYS rather quickly, but we'll be conservative on
		 * increasing VSYS.
		 */
		if (drive > drive_limit)
			drive = drive_limit;
		CPRINTS_DBG("drive = %d", drive);
	}

	CPRINTS_DBG("##DATA = %d %d %d %d %d %d %d", batt.desired_current,
		    batt.current, *desired_charger_input_current,
		    ocpc->secondary_ibus_ma, error, ocpc->last_vsys, drive);

	/*
	 * For the pre-charge phase, simply keep the VSYS target at the desired
	 * voltage.
	 */
	if (ph == PHASE_PRECHARGE)
		vsys_target = batt.desired_voltage;

	/*
	 * Adjust our VSYS target by applying the calculated drive.  Note that
	 * we won't apply our drive the first time through this function such
	 * that we can determine our initial error.
	 */
	if ((ocpc->last_vsys != OCPC_UNINIT) && (ph > PHASE_PRECHARGE))
		vsys_target = ocpc->last_vsys + drive;

	/*
	 * Once we're in the CV region, all we need to do is keep VSYS at the
	 * desired voltage.
	 */
	if (ph == PHASE_CV_TRIP) {
		vsys_target =
			batt.desired_voltage +
			((i_ma_CC_CV * ocpc->combined_rsys_rbatt_mo) / 1000);
		CPRINTS_DBG("i_ma_CC_CV = %d", i_ma_CC_CV);
	}
	if (ph == PHASE_CV_COMPLETE)
		vsys_target =
			batt.desired_voltage + ((batt_info->precharge_current *
						 ocpc->combined_rsys_rbatt_mo) /
						1000);

	/*
	 * Ensure VSYS is no higher than the specified maximum battery voltage
	 * plus the voltage drop across the system.
	 */
	vsys_target =
		CLAMP(vsys_target, min_vsys_target,
		      batt_info->voltage_max +
			      (i_ma * ocpc->combined_rsys_rbatt_mo / 1000));

	/* If we're input current limited, we cannot increase VSYS any more. */
	CPRINTS_DBG("OCPC: Inst. Input Current: %dmA (Limit: %dmA)",
		    ocpc->secondary_ibus_ma, *desired_charger_input_current);

	if (charger_is_icl_reached(chgnum, &icl_reached) != EC_SUCCESS) {
		/*
		 * If the charger doesn't support telling us, assume that the
		 * input current limit is reached if we're consuming more than
		 * 95% of the limit.
		 */
		if (ocpc->secondary_ibus_ma >=
		    (*desired_charger_input_current * 95 / 100))
			icl_reached = true;
	}

	if (icl_reached && (vsys_target > ocpc->last_vsys) &&
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
	 * Each position represents 5% of the target current, i.e. i_ma/20.
	 */
	if (i_ma != 0) {
		loc = error * 20 / i_ma;
		loc = CLAMP(loc, -10, 10);
		CPRINT_VIZ("[");
		for (i = -10; i <= 10; i++) {
			if (i == 0)
				CPRINT_VIZ("%c", loc == 0 ? '#' : '|');
			else
				CPRINT_VIZ("%c", i == loc ? 'o' : '-');
		}
		CPRINT_VIZ("] (actual)%dmA (desired)%dmA\n", batt.current,
			   i_ma);
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
	if (!charger_get_actual_voltage(CHARGER_PRIMARY, &val))
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
	if (!charger_get_actual_voltage(CHARGER_SECONDARY, &val))
		ocpc->vsys_aux_mv = val;

	val = 0;
	if (!charger_get_actual_current(CHARGER_SECONDARY, &val))
		ocpc->isys_ma = val;
}

__overridable void ocpc_get_pid_constants(int *kp, int *kp_div, int *ki,
					  int *ki_div, int *kd, int *kd_div)
{
}

static enum ec_error_list ocpc_precharge_enable(bool enable)
{
	/* Enable linear charging on the primary charger IC. */
	int rv = charger_enable_linear_charge(CHARGER_PRIMARY, enable);

	if (rv)
		CPRINTS("OCPC: Failed to %sble linear charge!",
			enable ? "ena" : "dis");

	return rv;
}

void ocpc_reset(struct ocpc_data *ocpc)
{
	struct batt_params batt;
	int voltage;

	battery_get_params(&batt);
	ocpc->integral = 0;
	ocpc->last_error = 0;
	ocpc->last_vsys = OCPC_UNINIT;

	/*
	 * Initialize the VSYS target on the aux chargers to the current battery
	 * voltage to avoid a large spike.
	 */
	if (ocpc->active_chg_chip > CHARGER_PRIMARY) {
		voltage = (batt.voltage > 0 &&
			   !(batt.flags & BATT_FLAG_BAD_VOLTAGE)) ?
				  batt.voltage :
				  battery_get_info()->voltage_normal;
#ifdef CONFIG_CHARGER_NARROW_VDC
		if (voltage < battery_get_info()->voltage_min)
			voltage = battery_get_info()->voltage_min;
#endif
		CPRINTS("OCPC: C%d Init VSYS to %dmV", ocpc->active_chg_chip,
			voltage);
		charger_set_voltage(ocpc->active_chg_chip, voltage);
	}
	/*
	 * See(b:191347747) When linear precharge is enabled, it may affect
	 * the charging behavior from the primary charger IC. Therefore as
	 * a part of the reset process, we need to disable linear precharge.
	 */
	ocpc_precharge_enable(false);
}

test_export_static void ocpc_set_pid_constants(void)
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

static int command_ocpcdebug(int argc, const char **argv)
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

static int command_ocpcpid(int argc, const char **argv)
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

static int command_ocpcdrvlmt(int argc, const char **argv)
{
	if (argc == 2) {
		drive_limit = atoi(argv[1]);
	}

	/* Print the current constants */
	ccprintf("Drive Limit = %d\n", drive_limit);
	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(ocpcdrvlmt, command_ocpcdrvlmt, "[<drive_limit>]",
			     "Show/Set drive limit for OCPC PID loop");

#ifdef TEST_BUILD
int test_ocpc_get_viz_output(void)
{
	return viz_output;
}
int test_ocpc_get_debug_output(void)
{
	return debug_output;
}
void test_ocpc_reset_resistance_state(void)
{
	for (int i = 0; i < NUM_RESISTANCE_SAMPLES; i++) {
		resistance_tbl[i][0] = CONFIG_OCPC_DEF_RBATT_MOHMS;
		resistance_tbl[i][1] = CONFIG_OCPC_DEF_RBATT_MOHMS;
		resistance_tbl[i][2] = 0;
	}

	resistance_tbl_idx = 0;

	memset(mean_resistance, 0, sizeof(mean_resistance));
	memset(stddev_resistance, 0, sizeof(stddev_resistance));
	memset(ub, 0, sizeof(ub));
	memset(lb, 0, sizeof(lb));
}
#endif
