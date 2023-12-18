


#include "charge_state.h"
#include "charger.h"
#include "charge_manager.h"
#include "chipset.h"
#include "common_cpu_power.h"
#include "customized_shared_memory.h"
#include "console.h"
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

static int battery_mwatt_type;
static int battery_current_limit_mA;
static int powerlimit_restore;
static int dc_safety_power_limit_level;

static void update_os_power_slider(int mode, int active_mpower)
{
	switch (mode) {
	case EC_DC_BEST_PERFORMANCE:
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL] = 30000;
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPPT] = 35000;
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_FPPT] = battery_mwatt_type - 15000;
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_P3T] = 80000;
		CPRINTS("DC BEST PERFORMANCE");
		break;
	case EC_DC_BALANCED:
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL] = 28000;
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPPT] = 33000;
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_FPPT] = battery_mwatt_type - 20000;
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_P3T] = 80000;
		CPRINTS("DC BALANCED");
		break;
	case EC_DC_BEST_EFFICIENCY:
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL] = 15000;
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPPT] = 25000;
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_FPPT] = 30000;
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_P3T] = 80000;
		CPRINTS("DC BEST EFFICIENCY");
		break;
	case EC_DC_BATTERY_SAVER:
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL] = 15000;
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPPT] = 15000;
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_FPPT] = 30000;
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_P3T] = battery_mwatt_type;
		CPRINTS("DC BATTERY SAVER");
		break;
	case EC_AC_BEST_PERFORMANCE:
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL] = 30000;
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPPT] = 35000;
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_FPPT] = 53000;
		/* AC p3t will limited by adapter_power_limit */
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_P3T] = 80000;
		CPRINTS("AC BEST PERFORMANCE");
		break;
	case EC_AC_BALANCED:
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL] = 28000;
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPPT] = 33000;
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_FPPT] = 51000;
		/* AC p3t will limited by adapter_power_limit */
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_P3T] = 80000;
		CPRINTS("AC BALANCED");
		break;
	case EC_AC_BEST_EFFICIENCY:
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL] = 15000;
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPPT] = 25000;
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_FPPT] = 30000;
		/* AC p3t will limited by adapter_power_limit */
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_P3T] = 80000;
		CPRINTS("AC BEST EFFICIENCY");
		break;
	default:
		/* no mode, run power table */
		break;
	}
}

static void update_adapter_power_limit(int battery_percent, int active_mpower, bool with_dc)
{
	if (with_dc && (battery_percent < 3) && active_mpower > 0) {
		power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 15000;
		power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 15000;
		power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 30000;
		power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] = active_mpower * 95 / 100;
		/* CPB enable */
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_POWER_LIMIT_EVENT) &= ~CPB_DISABLE;
		CPRINTS("DRAIN BATTERY");
		return;
	}

	if ((!with_dc) && (active_mpower >= 100000)) {
		/* AC (Without Battery) (ADP >= 100W) */
		power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 30000;
		power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 35000;
		power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 53000;
		power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] = 80000;
		/* CPB enable */
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_POWER_LIMIT_EVENT) &= ~CPB_DISABLE;
	} else if ((!with_dc) && (active_mpower >= 60000)) {
		/* AC (Without Battery) (60W <= ADP < 100W) */
		power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 30000;
		power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 33000;
		power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 35000;
		power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] = active_mpower * 95 / 100;
		/* CPB disable */
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_POWER_LIMIT_EVENT) &= ~CPB_DISABLE;
	} else if ((battery_percent < 30) && (active_mpower >= 55000)) {
		/* AC (With Battery) (Battery Capacity < 30%, ADP >= 55W) */
		power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 30000;
		power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = (active_mpower * 85 / 100) - 20000;
		power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = (active_mpower * 85 / 100) - 15000;
		power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
			MIN(176000, (active_mpower * 90 / 100) + 89000);
		/* CPB enable */
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_POWER_LIMIT_EVENT) &= ~CPB_DISABLE;
	} else if ((battery_percent >= 30) && (active_mpower >= 45000)) {
		/* AC (With Battery) (Battery Capacity >= 30%, ADP >= 45W) */
		power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 30000;
		power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 35000;
		power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 53000;
		power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
			MIN(176000, (active_mpower * 90 / 100) + 89000);
		/* CPB enable */
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_POWER_LIMIT_EVENT) &= ~CPB_DISABLE;
	} else {
		/* otherwise, take as DC only case */
		power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 30000;
		power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 35000;
		power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = battery_mwatt_type - 15000;
		/* DC mode p3t should follow os_power_slider */
		power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] = 89000;
		/* CPB enable */
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_POWER_LIMIT_EVENT) &= ~CPB_DISABLE;
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
		/* restore to slider mode */
		power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPL]
			= power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL];
		power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPPT]
			= power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPPT];
		power_limit[FUNCTION_SAFETY].mwatt[TYPE_FPPT]
			= power_limit[FUNCTION_SLIDER].mwatt[TYPE_FPPT];
		power_limit[FUNCTION_SAFETY].mwatt[TYPE_P3T]
			= power_limit[FUNCTION_SLIDER].mwatt[TYPE_P3T];
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
					== power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL]) {
					powerlimit_restore = 0;
					return;
				}
				delta = (ABS(battery_current - battery_current_limit_mA)
					* battery_voltage) * 12 / 10 / 1000;
				new_mwatt = new_mwatt + delta;

				power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPL] = MIN(new_mwatt,
					power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL]);
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
	static int old_slider_mode;
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

	if (old_slider_mode != mode) {
		old_slider_mode = mode;
		if (func_ctl & 0x1)
			update_os_power_slider(mode, active_mpower);
	}

	if (func_ctl & 0x2)
		update_adapter_power_limit(battery_percent, active_mpower, with_dc);

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
		/* use slider as default */
		target_func[item] = FUNCTION_SLIDER;
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
	const char *str = "FRANGWAT01";
	static int pre_battery_type;

	battery_mwatt_type =
		(!strncmp(battery_static[BATT_IDX_MAIN].model_ext, str, 10) ?
		BATTERY_61mW : BATTERY_55mW);

	if (pre_battery_type != battery_mwatt_type)
		pre_battery_type = battery_mwatt_type;
	else
		return;

	battery_current_limit_mA =
		((battery_mwatt_type == BATTERY_61mW) ? -3920 : -3570);

	/* initial slider table to battery balance as default */
	power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL] = 28000;
	power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPPT] = 33000;
	power_limit[FUNCTION_SLIDER].mwatt[TYPE_FPPT] =
		((battery_mwatt_type == BATTERY_61mW) ? 41000 : 35000);
	power_limit[FUNCTION_SLIDER].mwatt[TYPE_P3T] =
		((battery_mwatt_type == BATTERY_61mW) ? 70000 : 80000);
	power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_P3T];
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, initial_soc_power_limit, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, initial_soc_power_limit, HOOK_PRIO_DEFAULT);
