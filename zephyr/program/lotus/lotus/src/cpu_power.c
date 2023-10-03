


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
#include "gpu.h"


#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

static int battery_current_limit_mA;
static int powerlimit_restore;
static int slider_stt_table;
static int power_stt_table;
static int dc_safety_power_limit_level;

static void update_os_power_slider(int mode, bool with_dc, int active_mpower)
{
	switch (mode) {
	case EC_DC_BEST_PERFORMANCE:
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL] =
			(gpu_present() ? 60000 : 40000);
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPPT] =
			(gpu_present() ? 60000 : 48000);
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_FPPT] =
			(gpu_present() ? 60000 : 58000);
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_APU_ONLY_SPPT] =
			(gpu_present() ? 30000 : 0);
		slider_stt_table = (gpu_present() ? 21 : 23);
		CPRINTS("DC BEST PERFORMANCE");
		break;
	case EC_DC_BALANCED:
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL] =
			(gpu_present() ? 50000 : 30000);
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPPT] =
			(gpu_present() ? 50000 : 36000);
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_FPPT] =
			(gpu_present() ? 50000 : 44000);
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_APU_ONLY_SPPT] =
			(gpu_present() ? 20000 : 0);
		slider_stt_table = (gpu_present() ? 22 : 24);
		CPRINTS("DC BALANCED");
		break;
	case EC_DC_BEST_EFFICIENCY:
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL] =
			(gpu_present() ? 50000 : 20000);
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPPT] =
			(gpu_present() ? 50000 : 24000);
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_FPPT] =
			(gpu_present() ? 50000 : 29000);
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_APU_ONLY_SPPT] =
			(gpu_present() ? 20000 : 0);
		slider_stt_table = (gpu_present() ? 22 : 25);
		CPRINTS("DC BEST EFFICIENCY");
		break;
	case EC_DC_BATTERY_SAVER:
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL] = 20000;
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPPT] = 20000;
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_FPPT] = 20000;
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_APU_ONLY_SPPT] =
			(gpu_present() ? 20000 : 0);
		slider_stt_table = (gpu_present() ? 7 : 14);
		CPRINTS("DC BATTERY SAVER");
		break;
	case EC_AC_BEST_PERFORMANCE:
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL] =
			(gpu_present() ? 145000 : 45000);
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPPT] =
			(gpu_present() ? 145000 : 54000);
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_FPPT] =
			(gpu_present() ? 145000 : 65000);
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_APU_ONLY_SPPT] =
			(gpu_present() ? 54000 : 0);
		slider_stt_table = (gpu_present() ? 1 : 8);
		CPRINTS("AC BEST PERFORMANCE");
		break;
	case EC_AC_BALANCED:
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL] =
			(gpu_present() ? 95000 : 40000);
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPPT] =
			(gpu_present() ? 95000 : 48000);
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_FPPT] =
			(gpu_present() ? 95000 : 58000);
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_APU_ONLY_SPPT] =
			(gpu_present() ? 50000 : 0);
		slider_stt_table = (gpu_present() ? 2 : 9);
		CPRINTS("AC BALANCED");
		break;
	case EC_AC_BEST_EFFICIENCY:
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL] =
			(gpu_present() ? 85000 : 30000);
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPPT] =
			(gpu_present() ? 85000 : 36000);
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_FPPT] =
			(gpu_present() ? 85000 : 44000);
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_APU_ONLY_SPPT] =
			(gpu_present() ? 40000 : 0);
		slider_stt_table = (gpu_present() ? 3 : 10);
		CPRINTS("AC BEST EFFICIENCY");
		break;
	default:
		/* no mode, run power table */
		break;
	}
}

static void update_adapter_power_limit(int battery_percent, int active_mpower,
				       bool with_dc, int mode)
{
	if (gpu_present()) {
		if (active_mpower >= 180000) {
			/* limited by update_os_power_slider */
			power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] =
				power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL];
			power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] =
				power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPPT];
			power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] =
				power_limit[FUNCTION_SLIDER].mwatt[TYPE_FPPT];
			power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] =
				power_limit[FUNCTION_SLIDER].mwatt[TYPE_APU_ONLY_SPPT];
			power_stt_table = slider_stt_table;
		} else if (active_mpower >= 140000) {
			if (mode == EC_AC_BEST_PERFORMANCE) {
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 95000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 95000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 95000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 50000;
				power_stt_table = 4;
			} else if (mode == EC_AC_BALANCED) {
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 85000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 85000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 85000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 40000;
				power_stt_table = 15;
			} else {
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 60000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 60000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 60000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 30000;
				power_stt_table = 17;
			}
		} else if (active_mpower >= 100000) {
			if (mode == EC_AC_BEST_PERFORMANCE) {
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 85000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 85000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 85000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 40000;
				power_stt_table = 5;
			} else {
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 60000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 60000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 60000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 30000;
				power_stt_table = 16;
			}
		} else if ((active_mpower < 100000) && (active_mpower > 0)) {
			if ((mode == EC_AC_BEST_PERFORMANCE) || (mode == EC_AC_BALANCED)) {
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 60000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 60000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 60000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 30000;
				power_stt_table = 6;
			} else {

			}
		} else {
			/* DC only */
			/* limited by update_os_power_slider */
			power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] =
				power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL];
			power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] =
				power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPPT];
			power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] =
				power_limit[FUNCTION_SLIDER].mwatt[TYPE_FPPT];
			power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] =
				power_limit[FUNCTION_SLIDER].mwatt[TYPE_APU_ONLY_SPPT];
			power_stt_table = slider_stt_table;
		}
	} else {
		/* UMA */
		if (active_mpower >= 180000) {
			/* limited by update_os_power_slider */
			power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] =
				power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL];
			power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] =
				power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPPT];
			power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] =
				power_limit[FUNCTION_SLIDER].mwatt[TYPE_FPPT];
			power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
			power_stt_table = slider_stt_table;
		} else if (active_mpower > 100000) {
			if (mode == EC_AC_BEST_PERFORMANCE) {
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 45000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 54000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 65000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
				power_stt_table = 11;
			} else if (mode == EC_AC_BALANCED) {
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 40000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 48000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 58000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
				power_stt_table = 18;
			} else {
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 30000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 36000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 44000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
				power_stt_table = 19;
			}
		} else if (active_mpower >= 80000) {
			if (mode == EC_AC_BEST_PERFORMANCE) {
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 30000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 36000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 44000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
				power_stt_table = 12;
			} else {
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 30000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 30000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 30000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
				power_stt_table = 20;
			}
		} else if ((active_mpower < 80000) && (active_mpower > 0)) {
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 30000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 30000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 30000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
				power_stt_table = 13;
		} else {
			/* DC only */
			/* limited by update_os_power_slider */
			power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] =
				power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL];
			power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] =
				power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPPT];
			power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] =
				power_limit[FUNCTION_SLIDER].mwatt[TYPE_FPPT];
			power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
			power_stt_table = slider_stt_table;
		}
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
	static uint32_t old_ao_sppt;
	static int old_stt_table;
	int mode = *host_get_memmap(EC_MEMMAP_POWER_SLIDE);
	int active_mpower = cypd_get_ac_power();
	bool with_dc = ((battery_is_present() == BP_YES) ? true : false);
	int battery_percent = charge_get_percent();

	if ((*host_get_memmap(EC_MEMMAP_STT_TABLE_NUMBER)) == 0)
		old_stt_table = 0;

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
			update_os_power_slider(mode, with_dc, active_mpower);
	}

	if (func_ctl & 0x2)
		update_adapter_power_limit(battery_percent, active_mpower, with_dc, mode);

	if ((mode != 0) && (old_stt_table != power_stt_table) && (power_stt_table != 0)) {
		*host_get_memmap(EC_MEMMAP_STT_TABLE_NUMBER) = power_stt_table;
		old_stt_table = power_stt_table;
		host_set_single_event(EC_HOST_EVENT_STT_UPDATE);
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

	/* when trigger thermal warm, reduce TYPE_APU_ONLY_SPPT to 15W */
	if (gpu_present()) {
		if (thermal_warn_trigger())
			power_limit[FUNCTION_THERMAL].mwatt[TYPE_APU_ONLY_SPPT] = 15000;
		else
			power_limit[FUNCTION_THERMAL].mwatt[TYPE_SPL] = 0;
	}

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
		|| (power_limit[target_func[TYPE_APU_ONLY_SPPT]].mwatt[TYPE_APU_ONLY_SPPT]
			!= old_ao_sppt)
		|| set_pl_limit || force_update) {
		/* only set PL when it is changed */
		old_sustain_power_limit = power_limit[target_func[TYPE_SPL]].mwatt[TYPE_SPL];
		old_slow_ppt_limit = power_limit[target_func[TYPE_SPPT]].mwatt[TYPE_SPPT];
		old_fast_ppt_limit = power_limit[target_func[TYPE_FPPT]].mwatt[TYPE_FPPT];
		old_p3t_limit = power_limit[target_func[TYPE_P3T]].mwatt[TYPE_P3T];

		CPRINTF("Change SOC Power Limit: SPL %dmW, sPPT %dmW, fPPT %dmW, p3T %dmW, ",
			old_sustain_power_limit, old_slow_ppt_limit,
			old_fast_ppt_limit, old_p3t_limit);
		set_pl_limit = set_pl_limits(old_sustain_power_limit, old_fast_ppt_limit,
			old_slow_ppt_limit, old_p3t_limit);
		old_ao_sppt =
			power_limit[target_func[TYPE_APU_ONLY_SPPT]].mwatt[TYPE_APU_ONLY_SPPT];
		CPRINTF("ao_sppt %dmW\n", old_ao_sppt);
		if (!set_pl_limit)
			set_pl_limit = update_apu_only_sppt_limit(old_ao_sppt);
	}
}

static void initial_soc_power_limit(void)
{
	battery_current_limit_mA = -5490;

	/* initial slider table to battery balance as default */
	power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL] = 60000;
	power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPPT] = 60000;
	power_limit[FUNCTION_SLIDER].mwatt[TYPE_FPPT] = 60000;
	power_limit[FUNCTION_SLIDER].mwatt[TYPE_P3T] = 60000;
	power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] = 170000;
	power_limit[FUNCTION_SLIDER].mwatt[TYPE_APU_ONLY_SPPT] = 60000;
}
DECLARE_HOOK(HOOK_INIT, initial_soc_power_limit, HOOK_PRIO_INIT_I2C);
