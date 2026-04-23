


#include "charge_state.h"
#include "charger.h"
#include "charge_manager.h"
#include "chipset.h"
#include "common_cpu_power.h"
#include "customized_shared_memory.h"
#include "console.h"
#include "cpu_power.h"
#include "driver/sb_rmi.h"
#include "extpower.h"
#include "hooks.h"
#include "math_util.h"
#include "throttle_ap.h"
#include "util.h"


#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

#define BATTERY_55mW 55000
#define BATTERY_61mW 61000
#define BATTERY_74mW 74000

static int battery_mwatt_type;
static int battery_current_limit_mA;
static int powerlimit_restore;
static int dc_safety_power_limit_level;

void update_power_limit_value(int power_mode, int active_mpower, int mode)
{
	int active_power = active_mpower/1000;
	int arr_size = PMF_TABLE[power_mode].arr_size / sizeof(PMF_TABLE[power_mode].info[0]);

	for (int i = 0; i < arr_size; i++) {
		if (active_power >= PMF_TABLE[power_mode].info[i].active_power && mode ==
			PMF_TABLE[power_mode].info[i].slide) {
			power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
				PMF_TABLE[power_mode].info[i].pmf.P3T * 1000;
			power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] =
				PMF_TABLE[power_mode].info[i].pmf.fPPT * 1000;
			power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] =
				PMF_TABLE[power_mode].info[i].pmf.sPPT * 1000;
			power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] =
				PMF_TABLE[power_mode].info[i].pmf.SPL * 1000;
			break;
		}
	}
}

static void update_power_limit(int battery_percent, int active_mpower,
				       bool with_dc, int mode)
{
	if ((!with_dc) && (active_mpower >= 100000)) {
		/* AC only (Without Battery) (ADP >= 100W) */
		update_power_limit_value(AC_ONLY_MODE, active_mpower, mode);
	} else if ((!with_dc) && (active_mpower >= 60000)) {
		/* AC only (Without Battery) (60W <= ADP < 100W) */
		update_power_limit_value(AC_ONLY_MODE, active_mpower, mode);
		power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
		 MIN(power_limit[FUNCTION_POWER].mwatt[TYPE_P3T],
		 (active_mpower * 95 / 100));
	} else if ((battery_percent < 30) && (active_mpower >= 55000)) {
		/* AC+DC (With Battery) (Battery Capacity < 30%, ADP >= 55W) */
		update_power_limit_value(AC_DC_MODE, active_mpower, mode);
		power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
		 MIN(power_limit[FUNCTION_POWER].mwatt[TYPE_P3T],
		 ((active_mpower * 95 / 100) - 15000 + battery_mwatt_type));

		switch (mode) {
		case EC_AC_BEST_PERFORMANCE:
		case EC_AC_BALANCED:
			power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] =
			 MIN(power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT],
			 ((active_mpower * 85 / 100) - 20000));
			power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] =
			 MIN(power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT],
			 ((active_mpower * 85 / 100) - 15000));
			break;
		default:
			break;
		}
	} else if ((battery_percent >= 30) && (active_mpower >= 45000)) {
		/* AC+DC (With Battery) (Battery Capacity >= 30%, ADP >= 45W) */
		update_power_limit_value(AC_DC_MODE, active_mpower, mode);
		power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
		 MIN(power_limit[FUNCTION_POWER].mwatt[TYPE_P3T],
		 ((active_mpower * 95 / 100) - 15000 + battery_mwatt_type));
	} else {
		/* DC */
		/* Some conditions with AC will need to be set to DC mode,
		 * overwriting the mode for DC mode table search.
		 */
		if (active_mpower > 0) {
			if (mode == EC_AC_BEST_PERFORMANCE)
				mode = EC_DC_BEST_PERFORMANCE;
			else if (mode == EC_AC_BALANCED)
				mode = EC_DC_BALANCED;
			else
				mode = EC_DC_BEST_EFFICIENCY;
		}
		update_power_limit_value(DC_ONLY_MODE, active_mpower, mode);
	}
}

static void update_dc_safety_power_limit(void)
{
	int new_mwatt;
	int delta;
	const struct batt_params *batt = charger_current_battery_params();
	int battery_current = batt->current;
	int battery_voltage = battery_dynamic[BATT_IDX_MAIN].actual_voltage;

	if (!powerlimit_restore) {
		/* restore to function power mode */
		power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPL]
			= power_limit[FUNCTION_POWER].mwatt[TYPE_SPL];
		power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPPT]
			= power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT];
		power_limit[FUNCTION_SAFETY].mwatt[TYPE_FPPT]
			= power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT];
		power_limit[FUNCTION_SAFETY].mwatt[TYPE_P3T]
			= power_limit[FUNCTION_POWER].mwatt[TYPE_P3T];
		powerlimit_restore = 1;
	} else {
		new_mwatt = power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPL];
		/* start tuning PL by format */
		/* discharge, value compare based on negative*/
		if (battery_current < battery_current_limit_mA) {
			/*
			 * reduce apu power limit by
			 * (1.2*((battery current - 3.57)* battery voltage)
			 * (mA * mV = mW / 1000)
			 */
			delta = (ABS(battery_current - battery_current_limit_mA)
				* battery_voltage) * 12 / 10 / 1000;
			new_mwatt = new_mwatt - delta;
			power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPL]
				= MAX(new_mwatt, 15000);
			power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPPT]
				= power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPL];
			power_limit[FUNCTION_SAFETY].mwatt[TYPE_FPPT]
				= power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPL];
			CPRINTF("batt ocp, delta: %d, new PL: %d\n",
				delta, power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPL]);

			if (new_mwatt < 15000) {
				throttle_ap(THROTTLE_ON, THROTTLE_HARD,
					THROTTLE_SRC_BAT_DISCHG_CURRENT);
				dc_safety_power_limit_level = 1;
			}
		} else if (battery_current > (battery_current_limit_mA * 9 / 10)) {
			/*
			 * increase apu power limit by
			 * (1.2*((battery current - 3.57)* battery voltage)
			 */
			if (dc_safety_power_limit_level) {
				throttle_ap(THROTTLE_OFF, THROTTLE_HARD,
					THROTTLE_SRC_BAT_DISCHG_CURRENT);
				dc_safety_power_limit_level = 0;
			} else {
				if (power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPL]
					== power_limit[FUNCTION_POWER].mwatt[TYPE_SPL]) {
					powerlimit_restore = 0;
					return;
				}
				delta = (ABS(battery_current - battery_current_limit_mA)
					* battery_voltage) * 12 / 10 / 1000;
				new_mwatt = new_mwatt + delta;

				power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPL] = MIN(new_mwatt,
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL]);
				power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPPT]
					= power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPL];
				power_limit[FUNCTION_SAFETY].mwatt[TYPE_FPPT]
					= power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPL];
				CPRINTF("batt ocp recover, delta: %d, new PL: %d\n",
					delta, power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPL]);
			}
		}
	}
}

void update_soc_power_limit(bool force_update, bool force_no_adapter)
{
	static uint32_t old_sustain_power_limit;
	static uint32_t old_fast_ppt_limit;
	static uint32_t old_slow_ppt_limit;
	static uint32_t old_p3t_limit;
	static int set_pl_limit;
	int mode = *host_get_memmap(EC_MEMMAP_POWER_SLIDE);
	int active_mpower = charge_manager_get_power_limit_uw() / 1000;
	bool with_dc = ((battery_is_present() == BP_YES) ? true : false);
	int battery_percent = charge_get_percent();

	if (!chipset_in_state(CHIPSET_STATE_ON) || !get_apu_ready())
		return;

	if (mode_ctl)
		mode = mode_ctl;

	if (force_no_adapter || (!extpower_is_present())) {
		active_mpower = 0;
	}

	if (func_ctl & 0x2) {
		if (mode)
			update_power_limit(battery_percent, active_mpower, with_dc, mode);
	}

	if (active_mpower == 0) {
		if (func_ctl & 0x4)
			update_dc_safety_power_limit();
	} else {
		for (int item = TYPE_SPL; item < TYPE_COUNT; item++)
			power_limit[FUNCTION_SAFETY].mwatt[item] = 0;
		powerlimit_restore = 0;

		if (dc_safety_power_limit_level) {
			throttle_ap(THROTTLE_OFF, THROTTLE_HARD,
					THROTTLE_SRC_BAT_DISCHG_CURRENT);
			dc_safety_power_limit_level = 0;
		}
	}

	/* when trigger thermal warm, reduce SPPT to 15W */
	if (thermal_warn_trigger())
		power_limit[FUNCTION_THERMAL].mwatt[TYPE_SPPT] = 15000;
	else
		power_limit[FUNCTION_THERMAL].mwatt[TYPE_SPPT] = 0;

	/* choose the lowest one */
	for (int item = TYPE_SPL; item < TYPE_COUNT; item++) {
		/* use function power as default */
		target_func[item] = FUNCTION_POWER;
		for (int func = FUNCTION_DEFAULT; func < FUNCTION_COUNT; func++) {
			if (power_limit[func].mwatt[item] < 1)
				continue;
			if (power_limit[target_func[item]].mwatt[item]
				> power_limit[func].mwatt[item])
				target_func[item] = func;
		}
	}

	/* p3t follow power table */
	target_func[TYPE_P3T] = FUNCTION_POWER;

	if (power_limit[target_func[TYPE_SPL]].mwatt[TYPE_SPL] != old_sustain_power_limit
		|| power_limit[target_func[TYPE_FPPT]].mwatt[TYPE_FPPT] != old_fast_ppt_limit
		|| power_limit[target_func[TYPE_SPPT]].mwatt[TYPE_SPPT] != old_slow_ppt_limit
		|| power_limit[target_func[TYPE_P3T]].mwatt[TYPE_P3T] != old_p3t_limit
		|| set_pl_limit || force_update) {
		/* only set PL when it is changed */
		old_sustain_power_limit = power_limit[target_func[TYPE_SPL]].mwatt[TYPE_SPL];
		old_slow_ppt_limit = power_limit[target_func[TYPE_SPPT]].mwatt[TYPE_SPPT];
		old_fast_ppt_limit = power_limit[target_func[TYPE_FPPT]].mwatt[TYPE_FPPT];
		old_p3t_limit = power_limit[target_func[TYPE_P3T]].mwatt[TYPE_P3T];

		CPRINTF("Change SOC Power Limit: SPL %dmW, sPPT %dmW, fPPT %dmW, p3T %dmW\n",
			old_sustain_power_limit, old_slow_ppt_limit,
			old_fast_ppt_limit, old_p3t_limit);
		set_pl_limit = set_pl_limits(old_sustain_power_limit, old_fast_ppt_limit,
			old_slow_ppt_limit, old_p3t_limit);
	}
}

static void initial_soc_power_limit(void)
{
	const char *curr_model = battery_static[BATT_IDX_MAIN].model_ext;
	int battery_discharge_current = 0;
	static int pre_battery_type;

	if (!strncmp(curr_model, "FRANEDA", 7)) {
		battery_mwatt_type = BATTERY_74mW;
		battery_discharge_current = -4640;
	} else if (!strncmp(curr_model, "FRANGWAT01", 10)) {
		battery_mwatt_type = BATTERY_61mW;
		battery_discharge_current = -3920;
	} else {
		battery_mwatt_type = BATTERY_55mW;
		battery_discharge_current = -3570;
	}

	if (pre_battery_type != battery_mwatt_type)
		pre_battery_type = battery_mwatt_type;
	else
		return;

	battery_current_limit_mA = battery_discharge_current;
	CPRINTS("Battery %dWh current limit: %dmA", battery_mwatt_type, battery_current_limit_mA);

	/*battery type change switch DC only PMF table*/
	switch (battery_mwatt_type) {
	case BATTERY_74mW:
		PMF_TABLE[DC_ONLY_MODE].info = DC_ONLY_74W_PMF;
		break;
	case BATTERY_61mW:
		PMF_TABLE[DC_ONLY_MODE].info = DC_ONLY_61W_PMF;
		break;
	case BATTERY_55mW:
	default:
		PMF_TABLE[DC_ONLY_MODE].info = DC_ONLY_55W_PMF;
		break;
	}

	/* initial power table to battery balance as default */
	power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] =
	 PMF_TABLE[DC_ONLY_MODE].info[1].pmf.SPL * 1000;
	power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] =
	 PMF_TABLE[DC_ONLY_MODE].info[1].pmf.sPPT * 1000;
	power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] =
	 PMF_TABLE[DC_ONLY_MODE].info[1].pmf.fPPT * 1000;
	power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
	 PMF_TABLE[DC_ONLY_MODE].info[1].pmf.P3T * 1000;
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, initial_soc_power_limit, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, initial_soc_power_limit, HOOK_PRIO_DEFAULT);
