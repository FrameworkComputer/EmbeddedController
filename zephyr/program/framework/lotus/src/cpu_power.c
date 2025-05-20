


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
struct pmf_info AMD_GPU_AC_DC_PMF[] ={
	{220, EC_AC_BEST_PERFORMANCE, 1, {145, 145, 145, 54}},
	{220, EC_AC_BALANCED, 2, {120, 120, 120, 50}},
	{220, EC_AC_BEST_EFFICIENCY, 3, {95, 95, 95, 45}},
	{180, EC_AC_BEST_PERFORMANCE, 4, {120, 120, 120, 50}},
	{180, EC_AC_BALANCED, 5, {95, 95, 95, 45}},
	{180, EC_AC_BEST_EFFICIENCY, 6, {85, 85, 85, 40}},
	{140, EC_AC_BEST_PERFORMANCE, 7, {95, 95, 95, 50}},
	{140, EC_AC_BALANCED, 8, {85, 85, 85, 40}},
	{140, EC_AC_BEST_EFFICIENCY, 9, {60, 60, 60, 30}},
	{100, EC_AC_BEST_PERFORMANCE, 10, {85, 85, 85, 40}},
	{100, EC_AC_BALANCED, 11, {60, 60, 60, 30}},
	{100, EC_AC_BEST_EFFICIENCY, 11, {60, 60, 60, 30}},
	{80, EC_AC_BEST_PERFORMANCE, 12, {60, 60, 60, 30}},
	{80, EC_AC_BALANCED, 12, {60, 60, 60, 30}},
	{80, EC_AC_BEST_EFFICIENCY, 12, {60, 60, 60, 30}},
	{1, EC_AC_BEST_PERFORMANCE, 13, {60, 60, 60, 30}},
	{1, EC_AC_BALANCED, 13, {60, 60, 60, 30}},
	{1, EC_AC_BEST_EFFICIENCY, 13, {60, 60, 60, 30}},
};

struct pmf_info AMD_GPU_AC_ONLY_PMF[] ={
	{220, EC_AC_BEST_PERFORMANCE, 17, {60, 60, 60, 30}},
	{220, EC_AC_BALANCED, 18, {50, 50, 50, 30}},
	{220, EC_AC_BEST_EFFICIENCY, 19, {30, 30, 30, 30}},
	{180, EC_AC_BEST_PERFORMANCE, 20, {60, 60, 60, 30}},
	{180, EC_AC_BALANCED, 21, {50, 50, 50, 30}},
	{180, EC_AC_BEST_EFFICIENCY, 22, {30, 30, 30, 30}},
	{140, EC_AC_BEST_PERFORMANCE, 23, {60, 60, 60, 30}},
	{140, EC_AC_BALANCED, 24, {50, 50, 50, 30}},
	{140, EC_AC_BEST_EFFICIENCY, 25, {30, 30, 30, 30}},
	{100, EC_AC_BEST_PERFORMANCE, 26, {50, 50, 50, 30}},
	{100, EC_AC_BALANCED, 26, {50, 50, 50, 30}},
	{100, EC_AC_BEST_EFFICIENCY, 27, {30, 30, 30, 30}},
	{80, EC_AC_BEST_PERFORMANCE, 28, {30, 30, 30, 30}},
	{80, EC_AC_BALANCED, 28, {30, 30, 30, 30}},
	{80, EC_AC_BEST_EFFICIENCY, 28, {30, 30, 30, 30}},
	{1, EC_AC_BEST_PERFORMANCE, 29, {30, 30, 30, 30}},
	{1, EC_AC_BALANCED, 29, {30, 30, 30, 30}},
	{1, EC_AC_BEST_EFFICIENCY, 29, {30, 30, 30, 30}},
};

struct pmf_info AMD_GPU_DC_ONLY_PMF[] ={
	{0, EC_DC_BEST_PERFORMANCE, 14, {60, 60, 60, 30}},
	{0, EC_DC_BALANCED, 15, {50, 50, 50, 20}},
	{0, EC_DC_BEST_EFFICIENCY, 15, {50, 50, 50, 20}},
	{0, EC_DC_BATTERY_SAVER, 16, {20, 20, 20, 20}},
};

struct pmf_table AMD_GPU_PMF_TABLE[] = {
	[AC_DC_MODE] = {.info = AMD_GPU_AC_DC_PMF, .arr_size = sizeof(AMD_GPU_AC_DC_PMF)},
	[AC_ONLY_MODE] = {.info = AMD_GPU_AC_ONLY_PMF, .arr_size = sizeof(AMD_GPU_AC_DC_PMF)},
	[DC_ONLY_MODE] = {.info = AMD_GPU_DC_ONLY_PMF, .arr_size = sizeof(AMD_GPU_AC_DC_PMF)},
};

/**********************************************************
 * NV GPU PMF table
 **********************************************************/
struct pmf_info NV_GPU_AC_DC_PMF[] ={
	{220, EC_AC_BEST_PERFORMANCE, 59, {65, 54, 45, 0}},
	{220, EC_AC_BALANCED, 60, {58, 48, 40, 0}},
	{220, EC_AC_BEST_EFFICIENCY, 61, {44, 36, 30, 0}},
	{180, EC_AC_BEST_PERFORMANCE, 62, {65, 54, 45, 0}},
	{180, EC_AC_BALANCED, 63, {58, 48, 40, 0}},
	{180, EC_AC_BEST_EFFICIENCY, 64, {44, 36, 30, 0}},
	{140, EC_AC_BEST_PERFORMANCE, 65, {65, 50, 45, 0}},
	{140, EC_AC_BALANCED, 66, {58, 40, 40, 0}},
	{140, EC_AC_BEST_EFFICIENCY, 67, {44, 30, 30, 0}},
	{100, EC_AC_BEST_PERFORMANCE, 68, {65, 40, 40, 0}},
	{100, EC_AC_BALANCED, 69, {44, 30, 30, 0}},
	{100, EC_AC_BEST_EFFICIENCY, 69, {44, 30, 30, 0}},
	{80, EC_AC_BEST_PERFORMANCE, 70, {44, 30, 30, 0}},
	{80, EC_AC_BALANCED, 70, {44, 30, 30, 0}},
	{80, EC_AC_BEST_EFFICIENCY, 70, {44, 30, 30, 0}},
	{1, EC_AC_BEST_PERFORMANCE, 71, {30, 30, 30, 0}},
	{1, EC_AC_BALANCED, 71, {30, 30, 30, 0}},
	{1, EC_AC_BEST_EFFICIENCY, 71, {30, 30, 30, 0}},
};

struct pmf_info NV_GPU_AC_ONLY_PMF[] ={
	{220, EC_AC_BEST_PERFORMANCE, 75, {60, 30, 30, 0}},
	{220, EC_AC_BALANCED, 76, {58, 30, 30, 0}},
	{220, EC_AC_BEST_EFFICIENCY, 77, {30, 30, 30, 0}},
	{180, EC_AC_BEST_PERFORMANCE, 78, {60, 30, 30, 0}},
	{180, EC_AC_BALANCED, 79, {58, 30, 30, 0}},
	{180, EC_AC_BEST_EFFICIENCY, 80, {30, 30, 30, 0}},
	{140, EC_AC_BEST_PERFORMANCE, 81, {60, 30, 30, 0}},
	{140, EC_AC_BALANCED, 82, {50, 30, 30, 0}},
	{140, EC_AC_BEST_EFFICIENCY, 83, {30, 30, 30, 0}},
	{100, EC_AC_BEST_PERFORMANCE, 84, {50, 30, 30, 0}},
	{100, EC_AC_BALANCED, 84, {50, 30, 30, 0}},
	{100, EC_AC_BEST_EFFICIENCY, 85, {30, 30, 30, 0}},
	{80, EC_AC_BEST_PERFORMANCE, 86, {30, 30, 30, 0}},
	{80, EC_AC_BALANCED, 86, {30, 30, 30, 0}},
	{80, EC_AC_BEST_EFFICIENCY, 86, {30, 30, 30, 0}},
	{1, EC_AC_BEST_PERFORMANCE, 87, {30, 30, 30, 0}},
	{1, EC_AC_BALANCED, 87, {30, 30, 30, 0}},
	{1, EC_AC_BEST_EFFICIENCY, 87, {30, 30, 30, 0}},
};

struct pmf_info NV_GPU_DC_ONLY_PMF[] ={
	{0, EC_DC_BEST_PERFORMANCE, 72, {58, 30, 30, 0}},
	{0, EC_DC_BALANCED, 73, {44, 20, 20, 0}},
	{0, EC_DC_BEST_EFFICIENCY, 73, {44, 20, 20, 0}},
	{0, EC_DC_BATTERY_SAVER, 74, {20, 20, 20, 0}},
};

struct pmf_table NV_GPU_PMF_TABLE[] = {
	[AC_DC_MODE] = {.info = NV_GPU_AC_DC_PMF, .arr_size = sizeof(NV_GPU_AC_DC_PMF)},
	[AC_ONLY_MODE] = {.info = NV_GPU_AC_ONLY_PMF, .arr_size = sizeof(NV_GPU_AC_ONLY_PMF)},
	[DC_ONLY_MODE] = {.info = NV_GPU_DC_ONLY_PMF, .arr_size = sizeof(NV_GPU_DC_ONLY_PMF)},
};

/**********************************************************
 * UMA PMF table
 **********************************************************/
struct pmf_info UMA_GPU_AC_DC_PMF[] ={
	{220, EC_AC_BEST_PERFORMANCE, 30, {65, 54, 45, 0}},
	{220, EC_AC_BALANCED, 31, {58, 48, 40, 0}},
	{220, EC_AC_BEST_EFFICIENCY, 32, {44, 36, 30, 0}},
	{180, EC_AC_BEST_PERFORMANCE, 33, {65, 54, 45, 0}},
	{180, EC_AC_BALANCED, 34, {58, 48, 40, 0}},
	{180, EC_AC_BEST_EFFICIENCY, 35, {44, 36, 30, 0}},
	{140, EC_AC_BEST_PERFORMANCE, 36, {65, 50, 45, 0}},
	{140, EC_AC_BALANCED, 37, {58, 40, 40, 0}},
	{140, EC_AC_BEST_EFFICIENCY, 38, {44, 30, 30, 0}},
	{100, EC_AC_BEST_PERFORMANCE, 39, {65, 40, 40, 0}},
	{100, EC_AC_BALANCED, 40, {44, 30, 30, 0}},
	{100, EC_AC_BEST_EFFICIENCY, 40, {44, 30, 30, 0}},
	{80, EC_AC_BEST_PERFORMANCE, 41, {44, 30, 30, 0}},
	{80, EC_AC_BALANCED, 41, {44, 30, 30, 0}},
	{80, EC_AC_BEST_EFFICIENCY, 41, {44, 30, 30, 0}},
	{1, EC_AC_BEST_PERFORMANCE, 42, {30, 30, 30, 0}},
	{1, EC_AC_BALANCED, 42, {30, 30, 30, 0}},
	{1, EC_AC_BEST_EFFICIENCY, 42, {30, 30, 30, 0}},
};

struct pmf_info UMA_AC_ONLY_PMF[] ={
	{220, EC_AC_BEST_PERFORMANCE, 46, {60, 30, 30, 0}},
	{220, EC_AC_BALANCED, 47, {58, 30, 30, 0}},
	{220, EC_AC_BEST_EFFICIENCY, 48, {30, 30, 30, 0}},
	{180, EC_AC_BEST_PERFORMANCE, 49, {60, 30, 30, 0}},
	{180, EC_AC_BALANCED, 50, {58, 30, 30, 0}},
	{180, EC_AC_BEST_EFFICIENCY, 51, {30, 30, 30, 0}},
	{140, EC_AC_BEST_PERFORMANCE, 52, {60, 30, 30, 0}},
	{140, EC_AC_BALANCED, 53, {50, 30, 30, 0}},
	{140, EC_AC_BEST_EFFICIENCY, 54, {30, 30, 30, 0}},
	{100, EC_AC_BEST_PERFORMANCE, 55, {50, 30, 30, 0}},
	{100, EC_AC_BALANCED, 55, {50, 30, 30, 0}},
	{100, EC_AC_BEST_EFFICIENCY, 56, {30, 30, 30, 0}},
	{80, EC_AC_BEST_PERFORMANCE, 57, {30, 30, 30, 0}},
	{80, EC_AC_BALANCED, 57, {30, 30, 30, 0}},
	{80, EC_AC_BEST_EFFICIENCY, 57, {30, 30, 30, 0}},
	{1, EC_AC_BEST_PERFORMANCE, 58, {30, 30, 30, 0}},
	{1, EC_AC_BALANCED, 58, {30, 30, 30, 0}},
	{1, EC_AC_BEST_EFFICIENCY, 58, {30, 30, 30, 0}},
};

struct pmf_info UMA_DC_ONLY_PMF[] ={
	{0, EC_DC_BEST_PERFORMANCE, 43, {58, 30, 30, 0}},
	{0, EC_DC_BALANCED, 44, {44, 20, 20, 0}},
	{0, EC_DC_BEST_EFFICIENCY, 44, {44, 20, 20, 0}},
	{0, EC_DC_BATTERY_SAVER, 45, {20, 20, 20, 0}},
};

struct pmf_table UMA_PMF_TABLE[] = {
	[AC_DC_MODE] = {.info = UMA_GPU_AC_DC_PMF, .arr_size = sizeof(UMA_GPU_AC_DC_PMF)},
	[AC_ONLY_MODE] = {.info = UMA_AC_ONLY_PMF, .arr_size = sizeof(UMA_AC_ONLY_PMF)},
	[DC_ONLY_MODE] = {.info = UMA_DC_ONLY_PMF, .arr_size = sizeof(UMA_DC_ONLY_PMF)},
};

void update_power_limit_thermal_value(struct pmf_data *pmf) {
	power_limit[FUNCTION_THERMAL_PMF].mwatt[TYPE_FPPT] = pmf->fPPT * 1000;
	power_limit[FUNCTION_THERMAL_PMF].mwatt[TYPE_SPPT] = pmf->sPPT * 1000;
	power_limit[FUNCTION_THERMAL_PMF].mwatt[TYPE_SPL] = pmf->SPL * 1000;
	power_limit[FUNCTION_THERMAL_PMF].mwatt[TYPE_APU_ONLY_SPPT] =
		pmf->APU_only_sPPT * 1000;
}

void update_thermal_value(struct pmf_table *table, int active_mpower, bool with_dc, int mode) {
	enum power_supply_type power_mode;
	int active_power = active_mpower/1000;
	int arr_size = 0;

	if(with_dc && active_mpower == 0) {
		power_mode = DC_ONLY_MODE;
	} else if(with_dc && active_mpower > 0) {
		power_mode = AC_DC_MODE;
	} else {
		power_mode = AC_ONLY_MODE;
	}

	arr_size = table[power_mode].arr_size / sizeof(table[power_mode].info[0]);
	for(int i = 0; i<arr_size; i++) {
		if(active_power >= table[power_mode].info[i].active_power && mode ==
			table[power_mode].info[i].slide) {
			update_power_limit_thermal_value(&table[power_mode].info[i].pmf);
			thermal_stt_table = table[power_mode].info[i].table_num;
			break;
		}
	}
}

/* Update PL for thermal table pmf sheet : pmf */
static void update_thermal_power_limit(int battery_percent, int active_mpower,
				       bool with_dc, int mode)
{
	uint8_t gpu_vendor;
    gpu_vendor = *host_get_memmap(EC_CUSTOMIZED_MEMMAP_GPU_TYPE);

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

static int get_adapter_power_limit_index(int old_index, int battery_percent, int mode)
{
	/* at ERS only performance mode need to adjust limit */
	if (mode == EC_AC_BEST_PERFORMANCE) {
		if (battery_percent > 60)
			old_index = 0;
		else if (battery_percent < 30)
			old_index = 1;
	} else
		old_index = 1;

	return old_index;
}

static void update_adapter_power_limit(int battery_percent, int active_mpower,
				       bool with_dc, int mode)
{
	static int new_index;

	if (gpu_is_working()) {
		if (with_dc) {
			if (active_mpower >= 240000) {
				new_index =
					get_adapter_power_limit_index(new_index,
						battery_percent, mode);
				switch (new_index) {
				case 0:
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 145000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 145000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 145000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] =
						54000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
						MIN(227000, (active_mpower * 918 / 1000) +
						133740 - 30000 - 125000);
					break;
				case 1:
				default:
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 105000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 105000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 105000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] =
						50000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
						MIN(150000, (active_mpower * 918 / 1000) +
						55000 - 30000 - 95000);
					break;
				}
			} else if (active_mpower >= 180000) {
				new_index =
					get_adapter_power_limit_index(new_index,
						battery_percent, mode);
				switch (new_index) {
				case 0:
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 120000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 120000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 120000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] =
						54000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
						MIN(145000, (active_mpower * 918 / 1000) +
						133740 - 30000 - 125000);
					break;
				case 1:
				default:
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 95000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 95000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 95000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] =
						50000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
						MIN(95000, (active_mpower * 918 / 1000) +
						55000 - 30000 - 95000);
					break;
				}
			} else if (active_mpower >= 140000) {
				new_index =
					get_adapter_power_limit_index(new_index,
						battery_percent, mode);
				switch (new_index) {
				case 0:
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 95000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 95000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 95000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] =
						50000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
						MIN(138000, (active_mpower * 918 / 1000) +
						123000 - 30000 - 120000);
					break;
				case 1:
				default:
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 85000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 85000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 85000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] =
						40000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
						MIN(120000, (active_mpower * 918 / 1000) +
						75000 - 30000 - 85000);
					break;
				}
			} else if (active_mpower >= 100000) {
				new_index =
					get_adapter_power_limit_index(new_index,
						battery_percent, mode);
				switch (new_index) {
				case 0:
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 60000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 60000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 60000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] =
						30000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
						MIN(120000, (active_mpower * 918 / 1000) +
						123000 - 30000 - 100000);
					break;
				case 1:
				default:
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 40000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 40000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 40000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] =
						30000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
						MIN(100000, (active_mpower * 918 / 1000) +
						75000 - 30000 - 75000);
					break;
				}
			} else if (active_mpower >= 5000) {
				/* DC + AC under 100W */
				new_index =
					get_adapter_power_limit_index(new_index,
						battery_percent, mode);
				switch (new_index) {
				case 0:
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 60000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 60000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 60000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] =
						30000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] = 118000;
					break;
				case 1:
				default:
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 40000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 40000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 40000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] =
						20000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] = 100000;
					break;
				}
			} else {
				/* DC only */
				if (battery_percent > 30) {
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 60000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 60000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 60000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] =
						30000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] = 118000;
					new_index = 0;
				} else if (battery_percent > 25) {
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 50000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 50000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 50000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] =
						20000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] = 100000;
					new_index = 1;
				} else if (battery_percent > 20) {
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 40000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 40000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 40000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] =
						20000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] = 100000;
					new_index = 1;
				} else {
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 20000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 20000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 20000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] =
						20000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] = 65000;
					new_index = 1;
				}
			}
		} else {
		/* AC only */
			if (active_mpower >= 240000) {
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 120000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 120000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 120000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 54000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
					MIN(145000, (active_mpower * 918 / 1000) - 30000 - 60000);
			} else if (active_mpower >= 180000) {
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 60000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 60000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 60000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 30000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
					MIN(75000, (active_mpower * 918 / 1000) - 30000);
			} else if (active_mpower >= 140000) {
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 50000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 50000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 50000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 20000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
					75000;
			} else if (active_mpower >= 100000) {
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 30000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 30000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 30000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 30000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
					75000;
			} else {
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 0;
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 0;
				power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 0;
				power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
				power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] = 65000;
			}
		}
	} else {
		/* UMA */
		if (with_dc) {
			if (active_mpower >= 240000) {
				new_index =
					get_adapter_power_limit_index(new_index,
						battery_percent, mode);
				switch (new_index) {
				case 0:
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 45000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 54000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 65000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
						MIN(227000, (active_mpower * 918 / 1000)
							+ 133740 - 30000);
					break;
				case 1:
				default:
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 45000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 54000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 65000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
						MIN(227000, (active_mpower * 918 / 1000)
							+ 40000 - 30000);
					break;
				}
			} else if (active_mpower >= 180000) {
				new_index =
					get_adapter_power_limit_index(new_index,
						battery_percent, mode);
				switch (new_index) {
				case 0:
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 45000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 54000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 65000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
						MIN(227000, (active_mpower * 918 / 1000) + 133740 - 30000);
					break;
				case 1:
				default:
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 45000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 54000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 65000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
						MIN(170000, (active_mpower * 918 / 1000) + 40000 - 30000);
					break;
				}
			} else if (active_mpower >= 140000) {
				new_index =
					get_adapter_power_limit_index(new_index,
						battery_percent, mode);
				switch (new_index) {
				case 0:
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 45000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 54000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 65000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
						MIN(227000, (active_mpower * 918 / 1000) + 133740 - 30000);
					break;
				case 1:
				default:
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 40000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 48000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 58000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
						MIN(175000, (active_mpower * 918 / 1000) + 40000 - 30000);
					break;
				}
			} else if (active_mpower >= 65000) {
				new_index =
					get_adapter_power_limit_index(new_index,
						battery_percent, mode);
				switch (new_index) {
				case 0:
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 45000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 54000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 65000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
						MIN(187000, (active_mpower * 6885 / 10000) + 148600 - 30000);
					break;
				case 1:
				default:
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 30000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 36000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 44000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
						MIN(175000, (active_mpower * 6885 / 10000) + 45000 - 30000);
					break;
				}
			} else if (active_mpower >= 5000) {
				/* DC + AC under 65W */
				new_index =
					get_adapter_power_limit_index(new_index,
						battery_percent, mode);
				switch (new_index) {
				case 0:
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 45000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 54000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 65000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] = 118000;
					break;
				case 1:
				default:
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 30000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 36000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 44000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] = 65000;
					break;
				}
			} else {
				/*  DC only */
				if (battery_percent > 30) {
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 45000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 54000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 65000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] = 118000;
					new_index = 0;
				} else if (battery_percent > 25) {
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 30000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 36000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 44000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] = 100000;
					new_index = 1;
				} else if (battery_percent > 20) {
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 20000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 24000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 29000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] = 100000;
					new_index = 1;
				} else {
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 20000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 20000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 20000;
					power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
					power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] = 65000;
					new_index = 1;
				}
			}
		} else {
		/* AC only */
			if (active_mpower >= 240000) {
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 45000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 54000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 65000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
				power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
					MIN(227000, (active_mpower * 918 / 1000) - 30000);
			} else if (active_mpower >= 180000) {
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 45000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 54000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 65000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
				power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
					MIN(135000, (active_mpower * 918 / 1000) - 30000);
			} else if (active_mpower >= 140000) {
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 45000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 54000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 65000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
				power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
					MIN(100000, (active_mpower * 918 / 1000) - 30000);
			} else if (active_mpower >= 65000) {
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 30000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 36000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 44000;
				power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
				power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
					MIN(70000, (active_mpower - 30000));
			} else {
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 0;
				power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 0;
				power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 0;
				power_limit[FUNCTION_POWER].mwatt[TYPE_APU_ONLY_SPPT] = 0;
				power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] = 65000;
			}
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

static void update_safety_power_limit(int active_mpower)
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
		return;

	if (my_test_current != 0)
		average_current = my_test_current;

	/* discharge, value compare based on negative */
	if (average_current < battery_current_limit_mA)
		level_increase = 1;
	else if (average_current > (battery_current_limit_mA * 75 / 100))
		level_increase = 0;
	else
		return;

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

void update_d_notify(int active_mpower, bool with_dc)
{
	uint8_t gpu_vendor,d_notify;
	int active_power = active_mpower/1000;
    gpu_vendor = *host_get_memmap(EC_CUSTOMIZED_MEMMAP_GPU_TYPE);

	if (gpu_is_working() && gpu_vendor == GPU_NV_GN22) {
		if(active_power >= 180)
			d_notify = 1;
		else if(active_power < 180 && active_power>= 140)
			d_notify = 2;
		else if(active_power < 140 && active_power>= 100 && with_dc)
			d_notify = 3;
		else if(active_power < 100 && active_power>= 1 && with_dc)
			d_notify = 4;
		else
			d_notify = 5;

		*host_get_memmap(EC_MEMMAP_DGPU_DX_STATUS) = d_notify;
	} else
		*host_get_memmap(EC_MEMMAP_DGPU_DX_STATUS) = 0;
}

void update_soc_power_limit(bool force_update, bool force_no_adapter)
{
	static uint32_t old_sustain_power_limit;
	static uint32_t old_fast_ppt_limit;
	static uint32_t old_slow_ppt_limit;
	static uint32_t old_p3t_limit;
	static int set_pl_limit;
	static uint32_t old_ao_sppt;
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

	if (func_ctl & 0x1)
		update_thermal_power_limit(battery_percent, active_mpower, with_dc, mode);

	if (func_ctl & 0x2)
		update_adapter_power_limit(battery_percent, active_mpower, with_dc, mode);

	if (func_ctl & 0x4) {
		update_safety_power_limit(active_mpower);
	}

	if ((mode != 0) && (old_stt_table != thermal_stt_table) && (thermal_stt_table != 0)) {
		*host_get_memmap(EC_MEMMAP_STT_TABLE_NUMBER) = thermal_stt_table;
		old_stt_table = thermal_stt_table;
		update_d_notify(active_mpower, with_dc);
		host_set_single_event(EC_HOST_EVENT_STT_UPDATE);
	}

	/* when trigger thermal warm, reduce TYPE_APU_ONLY_SPPT to 45W */
	if (gpu_is_working()) {
		if (thermal_warn_trigger())
			power_limit[FUNCTION_THERMAL].mwatt[TYPE_APU_ONLY_SPPT] = 45000;
		else
			power_limit[FUNCTION_THERMAL].mwatt[TYPE_APU_ONLY_SPPT] = 0;
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
