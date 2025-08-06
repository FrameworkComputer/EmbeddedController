


#include "charge_state.h"
#include "charger.h"
#include "charge_manager.h"
#include "board_charger.h"
#include "board_battery.h"
#include "board_function.h"
#include "board_host_command.h"
#include "chipset.h"
#include "common_cpu_power.h"
#include "customized_shared_memory.h"
#include "console.h"
#include "cypress_pd_common.h"
#include "cpu_power.h"
#include "driver/sb_rmi.h"
#include "extpower.h"
#include "gpu.h"
#include "hooks.h"
#include "math_util.h"
#include "power.h"
#include "throttle_ap.h"
#include "util.h"
#include "gpu.h"


#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

static int battery_current_limit_mA;
static int thermal_stt_table;
static int safety_stt;
static uint8_t events;
static bool force_typec_1_5a_flag;

enum clear_reasons {
	PROCHOT_CLEAR_REASON_SUCCESS,
	PROCHOT_CLEAR_REASON_NOT_POWER,
	PROCHOT_CLEAR_REASON_FORCE,
};

enum power_supply_type {
	AC_DC_MODE,
	AC_ONLY_MODE,
	DC_ONLY_MODE,
};

struct pmf_data {
	uint8_t P3T;
	uint8_t fPPT;
	uint8_t sPPT;
	uint8_t SPL;
	uint8_t APU_only_sPPT;
} __packed;

/*
 * The active power should be the lower boundary of the watt interval.
 * ex: intervel 240w~220W, the active_power is 220.
 */
struct pmf_info {
	uint16_t active_power;
	enum power_slide_mode slide;
	uint16_t table_num;
	struct pmf_data pmf;
} __packed;

struct pmf_table {
	struct pmf_info *info;
	uint8_t arr_size;
} __packed;

/**********************************************************
 * AMD GPU PMF table
 **********************************************************/
struct pmf_info AMD_GPU_AC_DC_PMF[] = {
	{240, EC_AC_BEST_PERFORMANCE, 1, {227, 145, 145, 145, 54}},
	{240, EC_AC_BALANCED, 2, {150, 120, 120, 120, 50}},
	{240, EC_AC_BEST_EFFICIENCY, 3, {150, 95, 95, 95, 45}},
	{180, EC_AC_BEST_PERFORMANCE, 4, {145, 120, 120, 120, 50}},
	{180, EC_AC_BALANCED, 5, {145, 95, 95, 95, 45}},
	{180, EC_AC_BEST_EFFICIENCY, 6, {145, 85, 85, 85, 40}},
	{140, EC_AC_BEST_PERFORMANCE, 7, {138, 95, 95, 95, 50}},
	{140, EC_AC_BALANCED, 8, {120, 85, 85, 85, 40}},
	{140, EC_AC_BEST_EFFICIENCY, 9, {120, 60, 60, 60, 30}},
	{100, EC_AC_BEST_PERFORMANCE, 10, {120, 85, 85, 85, 40}},
	{100, EC_AC_BALANCED, 11, {100, 60, 60, 60, 30}},
	{100, EC_AC_BEST_EFFICIENCY, 11, {100, 60, 60, 60, 30}},
	{80, EC_AC_BEST_PERFORMANCE, 12, {118, 60, 60, 60, 30}},
	{80, EC_AC_BALANCED, 12, {100, 60, 60, 60, 30}},
	{80, EC_AC_BEST_EFFICIENCY, 12, {100, 60, 60, 60, 30}},
	{1, EC_AC_BEST_PERFORMANCE, 13, {100, 60, 60, 60, 30}},
	{1, EC_AC_BALANCED, 13, {100, 60, 60, 60, 30}},
	{1, EC_AC_BEST_EFFICIENCY, 13, {100, 60, 60, 60, 30}},
};

struct pmf_info AMD_GPU_AC_ONLY_PMF[] = {
	{240, EC_AC_BEST_PERFORMANCE, 14, {145, 60, 60, 60, 30}},
	{240, EC_AC_BALANCED, 15, {145, 50, 50, 50, 30}},
	{240, EC_AC_BEST_EFFICIENCY, 16, {145, 30, 30, 30, 30}},
	{180, EC_AC_BEST_PERFORMANCE, 17, {75, 60, 60, 60, 30}},
	{180, EC_AC_BALANCED, 18, {75, 50, 50, 50, 30}},
	{180, EC_AC_BEST_EFFICIENCY, 19, {75, 30, 30, 30, 30}},
	{140, EC_AC_BEST_PERFORMANCE, 20, {75, 60, 60, 60, 30}},
	{140, EC_AC_BALANCED, 21, {75, 50, 50, 50, 30}},
	{140, EC_AC_BEST_EFFICIENCY, 22, {75, 30, 30, 30, 30}},
	{100, EC_AC_BEST_PERFORMANCE, 23, {75, 50, 50, 50, 30}},
	{100, EC_AC_BALANCED, 23, {75, 50, 50, 50, 30}},
	{100, EC_AC_BEST_EFFICIENCY, 24, {75, 30, 30, 30, 30}},
	{80, EC_AC_BEST_PERFORMANCE, 25, {72, 30, 30, 30, 30}},
	{80, EC_AC_BALANCED, 25, {72, 30, 30, 30, 30}},
	{80, EC_AC_BEST_EFFICIENCY, 25, {72, 30, 30, 30, 30}},
	{1, EC_AC_BEST_PERFORMANCE, 26, {54, 30, 30, 30, 30}},
	{1, EC_AC_BALANCED, 26, {54, 30, 30, 30, 30}},
	{1, EC_AC_BEST_EFFICIENCY, 26, {54, 30, 30, 30, 30}},
};

struct pmf_info AMD_GPU_DC_ONLY_PMF[] = {
	{0, EC_DC_BEST_PERFORMANCE, 27, {76, 60, 60, 60, 30}},
	{0, EC_DC_BALANCED, 28, {76, 50, 50, 50, 20}},
	{0, EC_DC_BEST_EFFICIENCY, 28, {76, 50, 50, 50, 20}},
	{0, EC_DC_BATTERY_SAVER, 29, {65, 20, 20, 20, 20}},
};

struct pmf_table AMD_GPU_PMF_TABLE[] = {
	[AC_DC_MODE] = {.info = AMD_GPU_AC_DC_PMF, .arr_size = sizeof(AMD_GPU_AC_DC_PMF)},
	[AC_ONLY_MODE] = {.info = AMD_GPU_AC_ONLY_PMF, .arr_size = sizeof(AMD_GPU_AC_DC_PMF)},
	[DC_ONLY_MODE] = {.info = AMD_GPU_DC_ONLY_PMF, .arr_size = sizeof(AMD_GPU_AC_DC_PMF)},
};

/**********************************************************
 * NV GPU PMF table
 **********************************************************/
struct pmf_info NV_GPU_AC_DC_PMF[] = {
	{240, EC_AC_BEST_PERFORMANCE, 59, {216, 65, 54, 45, 0}},
	{240, EC_AC_BALANCED, 60, {216, 58, 48, 40, 0}},
	{240, EC_AC_BEST_EFFICIENCY, 61, {216, 44, 36, 30, 0}},
	{180, EC_AC_BEST_PERFORMANCE, 62, {216, 65, 54, 45, 0}},
	{180, EC_AC_BALANCED, 63, {216, 58, 48, 40, 0}},
	{180, EC_AC_BEST_EFFICIENCY, 64, {216, 44, 36, 30, 0}},
	{140, EC_AC_BEST_PERFORMANCE, 65, {202, 65, 50, 45, 0}},
	{140, EC_AC_BALANCED, 66, {202, 58, 40, 40, 0}},
	{140, EC_AC_BEST_EFFICIENCY, 67, {202, 44, 30, 30, 0}},
	{100, EC_AC_BEST_PERFORMANCE, 68, {167, 65, 40, 40, 0}},
	{100, EC_AC_BALANCED, 69, {167, 44, 30, 30, 0}},
	{100, EC_AC_BEST_EFFICIENCY, 69, {167, 44, 30, 30, 0}},
	{80, EC_AC_BEST_PERFORMANCE, 70, {148, 44, 30, 30, 0}},
	{80, EC_AC_BALANCED, 70, {148, 44, 30, 30, 0}},
	{80, EC_AC_BEST_EFFICIENCY, 70, {148, 44, 30, 30, 0}},
	{1, EC_AC_BEST_PERFORMANCE, 71, {131, 44, 30, 30, 0}},
	{1, EC_AC_BALANCED, 71, {131, 44, 30, 30, 0}},
	{1, EC_AC_BEST_EFFICIENCY, 71, {131, 44, 30, 30, 0}},
};

struct pmf_info NV_GPU_AC_ONLY_PMF[] = {
	{240, EC_AC_BEST_PERFORMANCE, 72, {216, 60, 30, 30, 0}},
	{240, EC_AC_BALANCED, 73, {216, 58, 30, 30, 0}},
	{240, EC_AC_BEST_EFFICIENCY, 74, {216, 30, 30, 30, 0}},
	{180, EC_AC_BEST_PERFORMANCE, 75, {162, 60, 30, 30, 0}},
	{180, EC_AC_BALANCED, 76, {162, 58, 30, 30, 0}},
	{180, EC_AC_BEST_EFFICIENCY, 77, {162, 30, 30, 30, 0}},
	{140, EC_AC_BEST_PERFORMANCE, 78, {126, 60, 30, 30, 0}},
	{140, EC_AC_BALANCED, 79, {126, 50, 30, 30, 0}},
	{140, EC_AC_BEST_EFFICIENCY, 80, {126, 30, 30, 30, 0}},
	{100, EC_AC_BEST_PERFORMANCE, 81, {90, 50, 30, 30, 0}},
	{100, EC_AC_BALANCED, 81, {90, 50, 30, 30, 0}},
	{100, EC_AC_BEST_EFFICIENCY, 82, {90, 30, 30, 30, 0}},
	{80, EC_AC_BEST_PERFORMANCE, 83, {72, 30, 30, 30, 0}},
	{80, EC_AC_BALANCED, 83, {72, 30, 30, 30, 0}},
	{80, EC_AC_BEST_EFFICIENCY, 83, {72, 30, 30, 30, 0}},
	{1, EC_AC_BEST_PERFORMANCE, 84, {54, 30, 30, 30, 0}},
	{1, EC_AC_BALANCED, 84, {54, 30, 30, 30, 0}},
	{1, EC_AC_BEST_EFFICIENCY, 84, {54, 30, 30, 30, 0}},
};

struct pmf_info NV_GPU_DC_ONLY_PMF[] = {
	{0, EC_DC_BEST_PERFORMANCE, 85, {76, 58, 30, 30, 0}},
	{0, EC_DC_BALANCED, 86, {76, 44, 20, 20, 0}},
	{0, EC_DC_BEST_EFFICIENCY, 86, {76, 44, 20, 20, 0}},
	{0, EC_DC_BATTERY_SAVER, 87, {76, 20, 20, 20, 0}},
};

struct pmf_table NV_GPU_PMF_TABLE[] = {
	[AC_DC_MODE] = {.info = NV_GPU_AC_DC_PMF, .arr_size = sizeof(NV_GPU_AC_DC_PMF)},
	[AC_ONLY_MODE] = {.info = NV_GPU_AC_ONLY_PMF, .arr_size = sizeof(NV_GPU_AC_ONLY_PMF)},
	[DC_ONLY_MODE] = {.info = NV_GPU_DC_ONLY_PMF, .arr_size = sizeof(NV_GPU_DC_ONLY_PMF)},
};

/**********************************************************
 * UMA PMF table
 **********************************************************/
struct pmf_info UMA_GPU_AC_DC_PMF[] = {
	{240, EC_AC_BEST_PERFORMANCE, 30, {216, 65, 54, 45, 0}},
	{240, EC_AC_BALANCED, 31, {216, 58, 48, 40, 0}},
	{240, EC_AC_BEST_EFFICIENCY, 32, {216, 44, 36, 30, 0}},
	{180, EC_AC_BEST_PERFORMANCE, 33, {216, 65, 54, 45, 0}},
	{180, EC_AC_BALANCED, 34, {216, 58, 48, 40, 0}},
	{180, EC_AC_BEST_EFFICIENCY, 35, {216, 44, 36, 30, 0}},
	{140, EC_AC_BEST_PERFORMANCE, 36, {202, 65, 50, 45, 0}},
	{140, EC_AC_BALANCED, 37, {202, 58, 40, 40, 0}},
	{140, EC_AC_BEST_EFFICIENCY, 38, {202, 44, 30, 30, 0}},
	{100, EC_AC_BEST_PERFORMANCE, 39, {167, 65, 40, 40, 0}},
	{100, EC_AC_BALANCED, 40, {167, 44, 30, 30, 0}},
	{100, EC_AC_BEST_EFFICIENCY, 40, {167, 44, 30, 30, 0}},
	{80, EC_AC_BEST_PERFORMANCE, 41, {148, 44, 30, 30, 0}},
	{80, EC_AC_BALANCED, 41, {148, 44, 30, 30, 0}},
	{80, EC_AC_BEST_EFFICIENCY, 41, {148, 44, 30, 30, 0}},
	{1, EC_AC_BEST_PERFORMANCE, 42, {131, 44, 30, 30, 0}},
	{1, EC_AC_BALANCED, 42, {131, 44, 30, 30, 0}},
	{1, EC_AC_BEST_EFFICIENCY, 42, {131, 44, 30, 30, 0}},
};

struct pmf_info UMA_AC_ONLY_PMF[] = {
	{240, EC_AC_BEST_PERFORMANCE, 43, {216, 60, 30, 30, 0}},
	{240, EC_AC_BALANCED, 44, {216, 58, 30, 30, 0}},
	{240, EC_AC_BEST_EFFICIENCY, 45, {216, 30, 30, 30, 0}},
	{180, EC_AC_BEST_PERFORMANCE, 46, {162, 60, 30, 30, 0}},
	{180, EC_AC_BALANCED, 47, {162, 58, 30, 30, 0}},
	{180, EC_AC_BEST_EFFICIENCY, 48, {162, 30, 30, 30, 0}},
	{140, EC_AC_BEST_PERFORMANCE, 49, {126, 60, 30, 30, 0}},
	{140, EC_AC_BALANCED, 50, {126, 50, 30, 30, 0}},
	{140, EC_AC_BEST_EFFICIENCY, 51, {126, 30, 30, 30, 0}},
	{100, EC_AC_BEST_PERFORMANCE, 52, {90, 50, 30, 30, 0}},
	{100, EC_AC_BALANCED, 52, {90, 50, 30, 30, 0}},
	{100, EC_AC_BEST_EFFICIENCY, 53, {90, 30, 30, 30, 0}},
	{80, EC_AC_BEST_PERFORMANCE, 54, {72, 30, 30, 30, 0}},
	{80, EC_AC_BALANCED, 54, {72, 30, 30, 30, 0}},
	{80, EC_AC_BEST_EFFICIENCY, 54, {72, 30, 30, 30, 0}},
	{1, EC_AC_BEST_PERFORMANCE, 55, {54, 30, 30, 30, 0}},
	{1, EC_AC_BALANCED, 55, {54, 30, 30, 30, 0}},
	{1, EC_AC_BEST_EFFICIENCY, 55, {54, 30, 30, 30, 0}},
};

struct pmf_info UMA_DC_ONLY_PMF[] = {
	{0, EC_DC_BEST_PERFORMANCE, 56, {76, 58, 30, 30, 0}},
	{0, EC_DC_BALANCED, 57, {76, 44, 20, 20, 0}},
	{0, EC_DC_BEST_EFFICIENCY, 57, {76, 44, 20, 20, 0}},
	{0, EC_DC_BATTERY_SAVER, 58, {76, 20, 20, 20, 0}},
};

struct pmf_table UMA_PMF_TABLE[] = {
	[AC_DC_MODE] = {.info = UMA_GPU_AC_DC_PMF, .arr_size = sizeof(UMA_GPU_AC_DC_PMF)},
	[AC_ONLY_MODE] = {.info = UMA_AC_ONLY_PMF, .arr_size = sizeof(UMA_AC_ONLY_PMF)},
	[DC_ONLY_MODE] = {.info = UMA_DC_ONLY_PMF, .arr_size = sizeof(UMA_DC_ONLY_PMF)},
};

void update_power_limit_thermal_value(struct pmf_data *pmf)
{
	power_limit[FUNCTION_THERMAL_PMF].mwatt[TYPE_P3T] = pmf->P3T * 1000;
	power_limit[FUNCTION_THERMAL_PMF].mwatt[TYPE_FPPT] = pmf->fPPT * 1000;
	power_limit[FUNCTION_THERMAL_PMF].mwatt[TYPE_SPPT] = pmf->sPPT * 1000;
	power_limit[FUNCTION_THERMAL_PMF].mwatt[TYPE_SPL] = pmf->SPL * 1000;
	power_limit[FUNCTION_THERMAL_PMF].mwatt[TYPE_APU_ONLY_SPPT] =
		pmf->APU_only_sPPT * 1000;
}

void update_thermal_value(struct pmf_table *table, int active_mpower, bool with_dc, int mode)
{
	enum power_supply_type power_mode;
	int active_power = active_mpower/1000;
	int arr_size = 0;

	if (with_dc && active_mpower == 0) {
		power_mode = DC_ONLY_MODE;
	} else if (with_dc && active_mpower > 0) {
		power_mode = AC_DC_MODE;
	} else {
		power_mode = AC_ONLY_MODE;
	}

	arr_size = table[power_mode].arr_size / sizeof(table[power_mode].info[0]);
	for (int i = 0; i < arr_size; i++) {
		if (active_power >= table[power_mode].info[i].active_power && mode ==
			table[power_mode].info[i].slide) {
			update_power_limit_thermal_value(&table[power_mode].info[i].pmf);
			thermal_stt_table = table[power_mode].info[i].table_num;
			break;
		}
	}
}

/* Update PL for thermal table pmf sheet : pmf */
static void update_thermal_power_limit(int battery_percent, int active_mpower,
				       bool with_dc, int mode, uint8_t gpu_vendor)
{
	if (!gpu_is_working()) {
		update_thermal_value(UMA_PMF_TABLE, active_mpower, with_dc, mode);
	} else {
		switch (gpu_vendor) {
		case GPU_AMD_R23M:
			update_thermal_value(AMD_GPU_PMF_TABLE, active_mpower, with_dc, mode);
			break;
		case GPU_NV_GN22:
			update_thermal_value(NV_GPU_PMF_TABLE, active_mpower, with_dc, mode);
			break;
		default:
			break;
		}
	}
}

static void tune_PLs(int delta)
{
	power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPL]
		= MAX(power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPL] + delta, 20000);
	power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPPT]
		= MAX(power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPPT] + delta, 20000);
	power_limit[FUNCTION_SAFETY].mwatt[TYPE_FPPT]
		= MAX(power_limit[FUNCTION_SAFETY].mwatt[TYPE_FPPT] + delta, 20000);
	power_limit[FUNCTION_SAFETY].mwatt[TYPE_P3T]
		= MAX(power_limit[FUNCTION_SAFETY].mwatt[TYPE_P3T] + delta, 20000);
	if (gpu_is_working())
		power_limit[FUNCTION_SAFETY].mwatt[TYPE_APU_ONLY_SPPT]
			= MAX(power_limit[FUNCTION_SAFETY].mwatt[TYPE_APU_ONLY_SPPT]
					+ delta, 20000);
}

static int update_safety_power_limit(int active_mpower)
{
	static uint8_t safety_level;
	static uint8_t level_increase;
	int delta;
	int average_current = get_average_battery_current();
	int battery_voltage = battery_dynamic[BATT_IDX_MAIN].actual_voltage;
	int rv;
	int mw_apu = power_limit[FUNCTION_THERMAL_PMF].mwatt[TYPE_APU_ONLY_SPPT];
	static timestamp_t wait_stable_time;
	static timestamp_t update_safety_timer;
	timestamp_t now = get_time();

	if (!timestamp_expired(wait_stable_time, &now) ||
		!timestamp_expired(update_safety_timer, &now))
		return -1;

	if (my_test_current != 0)
		average_current = my_test_current;

	/* discharge, value compare based on negative */
	if (average_current < battery_current_limit_mA)
		level_increase = 1;
	else if (average_current > (battery_current_limit_mA * 75 / 100))
		level_increase = 0;
	else
		return -1;

	switch (safety_level) {
	case LEVEL_NORMAL:
		/* follow thermal table */
		power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPL]
			= power_limit[FUNCTION_THERMAL_PMF].mwatt[TYPE_SPL];
		power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPPT]
			= power_limit[FUNCTION_THERMAL_PMF].mwatt[TYPE_SPPT];
		power_limit[FUNCTION_SAFETY].mwatt[TYPE_FPPT]
			= power_limit[FUNCTION_THERMAL_PMF].mwatt[TYPE_FPPT];
		power_limit[FUNCTION_SAFETY].mwatt[TYPE_P3T]
			= power_limit[FUNCTION_THERMAL_PMF].mwatt[TYPE_P3T];
		power_limit[FUNCTION_SAFETY].mwatt[TYPE_APU_ONLY_SPPT]
			= power_limit[FUNCTION_THERMAL_PMF].mwatt[TYPE_APU_ONLY_SPPT];

		if (level_increase)
			safety_level++;
		break;
	case LEVEL_STOP_CHARGE:
		/* stop charging */
		if (level_increase) {
			set_chg_ctrl_mode(CHARGE_CONTROL_IDLE);
			safety_level++;
		} else {
			set_chg_ctrl_mode(CHARGE_CONTROL_NORMAL);
			if (safety_level > 0)
				safety_level--;
		}
		break;
	case LEVEL_TUNE_PLS:
		/* tuning CPU and GPU PLs */
		if (gpu_is_working()) {
			delta = 10000;
			if (level_increase) {
				tune_PLs((-1) * delta);
				if ((power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPL] <= 60000)
					|| (power_limit[FUNCTION_SAFETY].mwatt[TYPE_APU_ONLY_SPPT]
						<= 30000)) {
					power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPL]
						= 45000;
					power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPPT]
						= 54000;
					power_limit[FUNCTION_SAFETY].mwatt[TYPE_FPPT]
						= 65000;
					power_limit[FUNCTION_SAFETY].mwatt[TYPE_APU_ONLY_SPPT]
						= 54000;
					safety_level++;
				}
			} else {
				tune_PLs(delta);
				if ((power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPL]
					 >= power_limit[FUNCTION_THERMAL_PMF].mwatt[TYPE_SPL])
					 || (power_limit[FUNCTION_SAFETY].mwatt[TYPE_APU_ONLY_SPPT]
						 >= mw_apu)) {
					safety_level--;
				}
			}
		} else {
			delta = (ABS(average_current - battery_current_limit_mA)
				* battery_voltage) * 8 / 10 / 1000;
			if (level_increase) {
				tune_PLs((-1) * delta);
				if (power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPL] <= 20000)
					safety_level = LEVEL_PROCHOT;
			} else {
				tune_PLs(delta);
				if (power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPL]
					>= power_limit[FUNCTION_THERMAL_PMF].mwatt[TYPE_SPL])
					safety_level--;
			}
		}

		/* wait the system stable */
		wait_stable_time.val = get_time().val + (5 * SECOND);
		break;
	case LEVEL_DISABLE_GPU:
		/* disable GPU and tune CPU PLs */
		if (gpu_is_working()) {
			if (level_increase) {
				tune_PLs(-10000);
				if (power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPL] <= 20000)
					safety_level++;
			} else {
				tune_PLs(10000);
				if (power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPL] >= 60000)
					safety_level--;
			}

			/* wait the system stable */
			wait_stable_time.val = get_time().val + (5 * SECOND);
		} else {
			if (level_increase)
				safety_level++;
			else
				safety_level--;
		}
		break;
	case LEVEL_PROCHOT:
		/* prochot */
		if (level_increase) {
			throttle_ap(THROTTLE_ON, THROTTLE_HARD, THROTTLE_SRC_BAT_DISCHG_CURRENT);
			thermal_stt_table = (gpu_is_working() ? 7 : 14);
			safety_stt = 1;
			safety_level++;
		} else {
			throttle_ap(THROTTLE_OFF, THROTTLE_HARD, THROTTLE_SRC_BAT_DISCHG_CURRENT);
			thermal_stt_table = (gpu_is_working() ? 7 : 14);
			safety_stt = 1;
			safety_level--;
		}
		break;
	case LEVEL_TYPEC_1_5A:
		if (level_increase) {
			force_typec_1_5a_flag = 1;
			for (int controller = 0; controller < PD_CHIP_COUNT; controller++) {
				for (int port = 0; port < 2; port++) {
					if (cypd_port_3a_status(controller, port)) {
						/*if device is 3A sink device
						 * foce current to 1.5A
						 */
						rv = cypd_modify_safety_power_1_5A(controller,
							port);
					}
				}
			}
			safety_level++;
		} else {
			force_typec_1_5a_flag = 0;
			safety_level--;
		}
		break;
	case LEVEL_COUNT:
		thermal_stt_table = (gpu_is_working() ? 7 : 14);
		if (!level_increase)
			safety_level--;
		break;
	default:
		safety_level = LEVEL_COUNT;
		break;
	}

	/* only check safety function per second */
	update_safety_timer.val = get_time().val + (1 * SECOND);

	if (safety_pwr_logging) {
		CPRINTS("increase = %d, level = %d, curr = %d", level_increase,
					safety_level, average_current);
		CPRINTS("SAFETY, SPL %dmW, fPPT %dmW, sPPT %dmW, p3T %dmW, ao_sppt %dmW",
					power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPL],
					power_limit[FUNCTION_SAFETY].mwatt[TYPE_FPPT],
					power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPPT],
					power_limit[FUNCTION_SAFETY].mwatt[TYPE_P3T],
					power_limit[FUNCTION_SAFETY].mwatt[TYPE_APU_ONLY_SPPT]);
	}

	return safety_level;
}

void force_clear_pmf_prochot(void)
{
	CPRINTS("pmf update timeout");
	update_pmf_events(0, 255);
}
DECLARE_DEFERRED(force_clear_pmf_prochot);

void update_pmf_events(uint8_t pd_event, int enable)
{
	static uint8_t pre_events;
	int power;

	switch (power_get_state()) {
	case POWER_S0:
	case POWER_S3S0:
	case POWER_S0ixS0: /* S0ix -> S0 */
		power = 1;
		break;
	default:
		power = 0;
	}

	/* We should not need to assert the prochot before apu ready to update pmf */
	if (!power || !get_apu_ready() || (enable == 255)) {
		pre_events = 0;
		pd_event = 0;
		events = 0;
		throttle_ap(THROTTLE_OFF, THROTTLE_HARD, THROTTLE_SRC_UPDATE_PMF);
		return;
	}

	if (enable)
		events |= pd_event;
	else
		events &= ~pd_event;

	if (pre_events != events) {
		CPRINTS("events = %d, pre_events = %d", events, pre_events);
		if (events) {
			throttle_ap(THROTTLE_ON, THROTTLE_HARD, THROTTLE_SRC_UPDATE_PMF);
			if (pd_event == BIT(PD_PROGRESS_ENTER_EPR_MODE))
				set_gpu_gpio(GPIO_FUNC_ACDC, 0);
			hook_call_deferred(&force_clear_pmf_prochot_data, 3 * SECOND);
		} else {
			throttle_ap(THROTTLE_OFF, THROTTLE_HARD, THROTTLE_SRC_UPDATE_PMF);
			if (pd_event == BIT(PD_PROGRESS_ENTER_EPR_MODE))
				set_gpu_gpio(GPIO_FUNC_ACDC, 1);
			hook_call_deferred(&force_clear_pmf_prochot_data, -1);
		}

		pre_events = events;
	}
}

void clear_prochot(enum clear_reasons reason)
{
	if (events & BIT(PD_PROGRESS_ENTER_EPR_MODE) && (cypd_get_ac_power() > 100000)) {
		/* wait charger to entry the bypass mode */
#ifdef CONFIG_BOARD_LOTUS
		if (charger_in_bypass_mode())
			update_pmf_events(BIT(PD_PROGRESS_ENTER_EPR_MODE), 0);
#else
		update_pmf_events(BIT(PD_PROGRESS_ENTER_EPR_MODE), 0);
#endif

	}

	if (events & BIT(PD_PROGRESS_EXIT_EPR_MODE))
		update_pmf_events(BIT(PD_PROGRESS_EXIT_EPR_MODE), 0);

	if (events & BIT(PD_PROGRESS_DISCONNECTED)) {
		/* if the adapter is disconnected, we should clear all events */
		update_pmf_events(0xff, 0);
	}
}

static uint8_t update_d_notify(int active_mpower, uint8_t gpu_vendor,
					enum power_safety_level safety_level)
{
	static uint8_t pre_d_notify;
	uint8_t d_notify;
	int active_power;

	if (gpu_is_working() && gpu_vendor == GPU_NV_GN22) {
		if (safety_level == LEVEL_TUNE_PLS) {
			d_notify = 4;
		} else if (safety_level >= LEVEL_DISABLE_GPU) {
			d_notify = 5;
		} else {
			active_power = active_mpower / 1000;
			if (active_power >= 180)
				d_notify = 1;
			else if (active_power < 180 && active_power >= 140)
				d_notify = 2;
			else
				d_notify = 3;
		}
	} else
		d_notify = 0;

	if (pre_d_notify != d_notify) {
		pre_d_notify = d_notify;
		*host_get_memmap(EC_MEMMAP_DGPU_DX_STATUS) = d_notify;
	}

	return d_notify;
}

enum power_slide_mode best_performance_power_plan(int battery_percent,
					int active_mpower, bool with_dc, int mode)
{
	/*
	 * For main board ERS best performance mode power plan define
	 */

	static bool force_balance;

	if (!with_dc) {
		force_balance = false;
		return mode;
	} else if (!force_balance && battery_percent > 30) {
		force_balance = false;
		return mode;
	} else if (force_balance && battery_percent > 60) {
		force_balance = false;
		return mode;
	} else if (force_balance && active_mpower == 0 && battery_percent >= 30) {
		/*remove active power*/
		force_balance = false;
		return mode;
	}

	if (mode == EC_AC_BEST_PERFORMANCE || mode == EC_DC_BEST_PERFORMANCE) {
		if (battery_percent < 30)
			force_balance = true;
	} else {
		force_balance = false;
	}

	if (force_balance && mode == EC_AC_BEST_PERFORMANCE) {
		return EC_AC_BALANCED;
	} else if (force_balance && mode == EC_DC_BEST_PERFORMANCE) {
		return EC_DC_BALANCED;
	}

	return mode;
}

void update_soc_power_limit(bool force_update, bool force_no_adapter)
{
	static uint32_t old_sustain_power_limit;
	static uint32_t old_fast_ppt_limit;
	static uint32_t old_slow_ppt_limit;
	static uint32_t old_p3t_limit;
	static int set_pl_limit;
	static uint32_t old_ao_sppt;
	uint8_t gpu_vendor;
	uint8_t d_notify;
	static uint8_t old_d_notify;
	int safety_level;
	static uint8_t old_safety_level;
	static int old_stt_table;
	int mode = *host_get_memmap(EC_MEMMAP_POWER_SLIDE);
	int active_mpower = cypd_get_ac_power();
	bool with_dc = (((battery_is_present() == BP_YES) &&
			!battery_cutoff_in_progress() && !battery_is_cut_off()) ? true : false);
	int battery_percent = get_system_percentage() / 10;

	if ((*host_get_memmap(EC_MEMMAP_STT_TABLE_NUMBER)) == 0)
		old_stt_table = 0;

	if (!chipset_in_state(CHIPSET_STATE_ON) || !get_apu_ready()) {
		clear_prochot(PROCHOT_CLEAR_REASON_NOT_POWER);
		return;
	}

#ifdef CONFIG_BOARD_LOTUS
	if (force_update && charger_in_bypass_mode() && !get_gpu_gpio(GPIO_FUNC_ACDC))
		set_gpu_gpio(GPIO_FUNC_ACDC, 1);
#else
	if (force_update && !get_gpu_gpio(GPIO_FUNC_ACDC))
		set_gpu_gpio(GPIO_FUNC_ACDC, 1);
#endif

	if (mode_ctl)
		mode = mode_ctl;

	if (force_no_adapter || (!extpower_is_present())) {
		active_mpower = 0;
	}

	gpu_vendor = *host_get_memmap(EC_CUSTOMIZED_MEMMAP_GPU_TYPE);

	if (func_ctl & 0x1) {
		mode = best_performance_power_plan(battery_percent, active_mpower, with_dc, mode);
		update_thermal_power_limit(battery_percent, active_mpower, with_dc, mode,
					gpu_vendor);
	}

	if (func_ctl & 0x4) {
		safety_level = update_safety_power_limit(active_mpower);
		if (safety_level != -1)
			old_safety_level = safety_level;

		d_notify = update_d_notify(active_mpower, gpu_vendor, old_safety_level);
	}

	if (((mode != 0) && (old_stt_table != thermal_stt_table) && (thermal_stt_table != 0))
		|| (old_d_notify != d_notify)) {
		*host_get_memmap(EC_MEMMAP_STT_TABLE_NUMBER) = thermal_stt_table;
		old_stt_table = thermal_stt_table;
		old_d_notify = d_notify;
		host_set_single_event(EC_HOST_EVENT_STT_UPDATE);
	}

	/*
	 * when trigger thermal warm
	 * AMD GPU sku reduce TYPE_APU_ONLY_SPPT to 45W
	 * NV GPU sku reduce TYPE_SPPT to 45W
	 */
	if (gpu_is_working()) {
		if (thermal_warn_trigger()) {
			if (gpu_vendor == GPU_AMD_R23M)
				power_limit[FUNCTION_THERMAL].mwatt[TYPE_APU_ONLY_SPPT] = 45000;
			else if (gpu_vendor == GPU_NV_GN22)
				power_limit[FUNCTION_THERMAL].mwatt[TYPE_SPPT] = 45000;

		} else {
			power_limit[FUNCTION_THERMAL].mwatt[TYPE_APU_ONLY_SPPT] = 0;
			power_limit[FUNCTION_THERMAL].mwatt[TYPE_SPPT] = 0;
		}
	}

	for (int item = TYPE_SPL; item < TYPE_COUNT; item++) {
		/* use slider as default */
		target_func[item] = FUNCTION_THERMAL_PMF;
		for (int func = FUNCTION_DEFAULT; func < FUNCTION_COUNT; func++) {
			/* Ignored the zero value */
			if (power_limit[func].mwatt[item] < 1)
				continue;

			/* choose the lowest one */
			if (power_limit[target_func[item]].mwatt[item]
				> power_limit[func].mwatt[item])
				target_func[item] = func;
		}
	}

	if (power_limit[target_func[TYPE_SPL]].mwatt[TYPE_SPL] != old_sustain_power_limit
		|| power_limit[target_func[TYPE_FPPT]].mwatt[TYPE_FPPT] != old_fast_ppt_limit
		|| power_limit[target_func[TYPE_SPPT]].mwatt[TYPE_SPPT] != old_slow_ppt_limit
		|| power_limit[target_func[TYPE_P3T]].mwatt[TYPE_P3T] != old_p3t_limit
		|| (power_limit[target_func[TYPE_APU_ONLY_SPPT]].mwatt[TYPE_APU_ONLY_SPPT]
			!= old_ao_sppt)
		|| set_pl_limit || force_update || events) {
		/* only set PL when it is changed */
		old_sustain_power_limit = power_limit[target_func[TYPE_SPL]].mwatt[TYPE_SPL];
		old_slow_ppt_limit = power_limit[target_func[TYPE_SPPT]].mwatt[TYPE_SPPT];
		old_fast_ppt_limit = power_limit[target_func[TYPE_FPPT]].mwatt[TYPE_FPPT];
		old_p3t_limit = power_limit[target_func[TYPE_P3T]].mwatt[TYPE_P3T];

		set_pl_limit = set_pl_limits(old_sustain_power_limit, old_fast_ppt_limit,
			old_slow_ppt_limit, old_p3t_limit);

		if (!set_pl_limit) {
			old_ao_sppt =
			power_limit[target_func[TYPE_APU_ONLY_SPPT]].mwatt[TYPE_APU_ONLY_SPPT];
			set_pl_limit = update_apu_only_sppt_limit(old_ao_sppt);
		}

		if (!set_pl_limit) {
			/* Update PMF success, print the setting and de-asssert the prochot */
			CPRINTS("PMF: SPL %dmW, sPPT %dmW, fPPT %dmW, p3T %dmW, ao_sppt %dmW",
			old_sustain_power_limit, old_slow_ppt_limit,
			old_fast_ppt_limit, old_p3t_limit, old_ao_sppt);

			clear_prochot(PROCHOT_CLEAR_REASON_SUCCESS);
		}
	}
}

static void initial_soc_power_limit(void)
{
	battery_current_limit_mA = -5490;

	/* initial thermal table to battery balance as default */
	uint8_t DC_BALANCED_INDEX = 1;

	update_power_limit_thermal_value(&UMA_DC_ONLY_PMF[DC_BALANCED_INDEX].pmf);
}
DECLARE_HOOK(HOOK_INIT, initial_soc_power_limit, HOOK_PRIO_INIT_I2C);

bool safety_force_typec_1_5A(void)
{
	return force_typec_1_5a_flag;
}
