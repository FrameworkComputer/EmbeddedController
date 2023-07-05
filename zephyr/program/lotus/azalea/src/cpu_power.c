


#include "charge_state.h"
#include "charger.h"
#include "charge_manager.h"
#include "chipset.h"
#include "cpu_power.h"
#include "customized_shared_memory.h"
#include "console.h"
#include "driver/sb_rmi.h"
#include "extpower.h"
#include "hooks.h"
#include "math_util.h"
#include "util.h"


#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

static struct power_limit_details power_limit[FUNCTION_COUNT];
uint32_t ports_cost; /* TODO (0.9 * (total port cost)) */
bool manual_ctl;
static int battery_mwatt_type;
static int battery_p3t_mwatt;
static int battery_current_limit_mA;
static int target_function;
static int powerlimit_restore;

static int update_sustained_power_limit(uint32_t mwatt)
{
	uint32_t msgIn = 0;
	uint32_t msgOut;

	msgIn = mwatt;

	return sb_rmi_mailbox_xfer(SB_RMI_WRITE_SUSTAINED_POWER_LIMIT_CMD, msgIn, &msgOut);
}

static int update_flow_ppt_limit(uint32_t mwatt)
{
	uint32_t msgIn = 0;
	uint32_t msgOut;

	msgIn = mwatt;

	return sb_rmi_mailbox_xfer(SB_RMI_WRITE_FAST_PPT_LIMIT_CMD, msgIn, &msgOut);
}

static int update_slow_ppt_limit(uint32_t mwatt)
{
	uint32_t msgIn = 0;
	uint32_t msgOut;

	msgIn = mwatt;

	return sb_rmi_mailbox_xfer(SB_RMI_WRITE_SLOW_PPT_LIMIT_CMD, msgIn, &msgOut);
}

static int update_peak_package_power_limit(uint32_t mwatt)
{
	uint32_t msgIn = 0;
	uint32_t msgOut;

	msgIn = mwatt;

	return sb_rmi_mailbox_xfer(SB_RMI_WRITE_P3T_LIMIT_CMD, msgIn, &msgOut);
}

static void set_pl_limits(uint32_t spl, uint32_t fppt, uint32_t sppt, uint32_t p3t)
{
	update_sustained_power_limit(spl);
	update_flow_ppt_limit(fppt);
	update_slow_ppt_limit(sppt);
	update_peak_package_power_limit(p3t);
}

static void update_os_power_slider(int mode, int with_dc, int active_mpower)
{
	power_limit[FUNCTION_SLIDER].p3t_mwatt = battery_p3t_mwatt - POWER_DELTA - ports_cost;

	switch (mode) {
	case EC_DC_BEST_PERFORMANCE:
		power_limit[FUNCTION_SLIDER].spl_mwatt = 30000;
		power_limit[FUNCTION_SLIDER].sppt_mwatt = 35000;
		power_limit[FUNCTION_SLIDER].fppt_mwatt =
			((battery_mwatt_type == BATTERY_55mW) ? 35000 : 41000);
		CPRINTS("DC BEST PERFORMANCE");
		break;
	case EC_DC_BALANCED:
		power_limit[FUNCTION_SLIDER].spl_mwatt = 28000;
		power_limit[FUNCTION_SLIDER].sppt_mwatt = 33000;
		power_limit[FUNCTION_SLIDER].fppt_mwatt =
			((battery_mwatt_type == BATTERY_55mW) ? 35000 : 41000);
		CPRINTS("DC BALANCED");
		break;
	case EC_DC_BEST_EFFICIENCYE:
		power_limit[FUNCTION_SLIDER].spl_mwatt = 15000;
		power_limit[FUNCTION_SLIDER].sppt_mwatt = 20000;
		power_limit[FUNCTION_SLIDER].fppt_mwatt = 30000;
		CPRINTS("DC BEST EFFICIENCYE");
		break;
	case EC_DC_BATTERY_SAVER:
		power_limit[FUNCTION_SLIDER].spl_mwatt = 15000;
		power_limit[FUNCTION_SLIDER].sppt_mwatt = 15000;
		power_limit[FUNCTION_SLIDER].fppt_mwatt = 30000;
		CPRINTS("DC BATTERY SAVER");
		break;
	case EC_AC_BEST_PERFORMANCE:
		power_limit[FUNCTION_SLIDER].spl_mwatt = 30000;
		power_limit[FUNCTION_SLIDER].sppt_mwatt =
			(with_dc ? 35000 : (MIN(35000, ((active_mpower - 15000) * 9 / 10))));
		power_limit[FUNCTION_SLIDER].fppt_mwatt = 53000;
		CPRINTS("AC BEST PERFORMANCE");
		break;
	case EC_AC_BALANCED:
		power_limit[FUNCTION_SLIDER].spl_mwatt = 28000;
		power_limit[FUNCTION_SLIDER].sppt_mwatt =
			(with_dc ? 33000 : (MIN(33000, ((active_mpower - 15000) * 9 / 10))));
		power_limit[FUNCTION_SLIDER].fppt_mwatt =
			(with_dc ? 51000 : (MIN(51000, (active_mpower - 15000))));
		CPRINTS("AC BALANCED");
		break;
	case EC_AC_BEST_EFFICIENCYE:
		power_limit[FUNCTION_SLIDER].spl_mwatt = (with_dc ? 15000 : 28000);
		power_limit[FUNCTION_SLIDER].sppt_mwatt =
			(with_dc ? 25000 : (MIN(33000, ((active_mpower - 15000) * 9 / 10))));
		power_limit[FUNCTION_SLIDER].fppt_mwatt =
			(with_dc ? 30000 : (MIN(51000, (active_mpower - 15000))));
		CPRINTS("AC BEST EFFICIENCYE");
		break;
	default:
		/* no mode, run power table */
		break;
	}
}

static void update_power_power_limit(int battery_percent, int active_mpower)
{
	if ((active_mpower < 55000)) {
		/* dc mode (active_mpower == 0) or AC < 55W */
		power_limit[FUNCTION_POWER].spl_mwatt = 30000;
		power_limit[FUNCTION_POWER].sppt_mwatt =
			battery_mwatt_type - POWER_DELTA - ports_cost;
		power_limit[FUNCTION_POWER].fppt_mwatt =
			battery_mwatt_type - POWER_DELTA - ports_cost;
		power_limit[FUNCTION_POWER].p3t_mwatt =
			battery_p3t_mwatt - POWER_DELTA - ports_cost;
	} else if (battery_percent > 40) {
		/* ADP > 55W and Battery percentage > 40% */
		power_limit[FUNCTION_POWER].spl_mwatt = 30000;
		power_limit[FUNCTION_POWER].sppt_mwatt =
			MIN(43000, (active_mpower * 95 / 100) + battery_mwatt_type - POWER_DELTA - ports_cost);
		power_limit[FUNCTION_POWER].fppt_mwatt =
			MIN(53000, (active_mpower * 95 / 100) + battery_mwatt_type - POWER_DELTA - ports_cost);
		power_limit[FUNCTION_POWER].p3t_mwatt =
			(active_mpower * 95 / 100) + battery_mwatt_type - POWER_DELTA - ports_cost;
	} else {
		/* ADP > 55W and Battery percentage <= 40% */
		power_limit[FUNCTION_POWER].spl_mwatt = 30000;
		power_limit[FUNCTION_POWER].sppt_mwatt =
			MIN(43000, (active_mpower * 95 / 100) - POWER_DELTA - ports_cost);
		power_limit[FUNCTION_POWER].fppt_mwatt =
			MIN(53000, (active_mpower * 95 / 100) - POWER_DELTA - ports_cost);
		power_limit[FUNCTION_POWER].p3t_mwatt =
			(active_mpower * 95 / 100) - POWER_DELTA - ports_cost;
	}
}

static void update_dc_safety_power_limit(void)
{
	static int powerlimit_level;

	int new_mwatt;
	int delta;
	const struct batt_params *batt = charger_current_battery_params();
	int battery_current = batt->current;
	int battery_voltage = battery_dynamic[BATT_IDX_MAIN].actual_voltage;

	if (!powerlimit_restore) {
		/* restore to slider mode */
		power_limit[FUNCTION_SAFETY].spl_mwatt = power_limit[FUNCTION_SLIDER].spl_mwatt;
		power_limit[FUNCTION_SAFETY].sppt_mwatt = power_limit[FUNCTION_SLIDER].sppt_mwatt;
		power_limit[FUNCTION_SAFETY].fppt_mwatt = power_limit[FUNCTION_SLIDER].fppt_mwatt;
		power_limit[FUNCTION_SAFETY].p3t_mwatt = power_limit[FUNCTION_SLIDER].p3t_mwatt;
		powerlimit_restore = 1;
	} else {
		new_mwatt = power_limit[FUNCTION_SAFETY].spl_mwatt;
		/* start tuning PL by formate */
		/* discharge, value compare based on negative*/
		if (battery_current < battery_current_limit_mA) {
			/*
			 * reduce apu power limit by
			 * (1.2*((battery current - 3.57)* battery voltage)
			 * (mA * mV = mW / 1000)
			 */
			delta = (ABS(battery_current - battery_current_limit_mA) * battery_voltage) * 12 / 10 / 1000;
			new_mwatt = new_mwatt - delta;
			power_limit[FUNCTION_SAFETY].spl_mwatt = MAX(new_mwatt, 15000);
			power_limit[FUNCTION_SAFETY].sppt_mwatt = power_limit[FUNCTION_SAFETY].spl_mwatt;
			power_limit[FUNCTION_SAFETY].fppt_mwatt = power_limit[FUNCTION_SAFETY].spl_mwatt;
			CPRINTF("batt ocp, delta: %d, new PL: %d\n", delta, power_limit[FUNCTION_SAFETY].spl_mwatt);

			if (new_mwatt < 15000) {
				chipset_throttle_cpu(1);
				powerlimit_level = 1;
				CPRINTF("batt ocp, prochot\n");
			}
		} else if (battery_current > (battery_current_limit_mA * 9 / 10)) {
			/*
			 * increase apu power limit by
			 * (1.2*((battery current - 3.57)* battery voltage)
			 */
			if (powerlimit_level) {
				chipset_throttle_cpu(0);
				CPRINTF("batt ocp, recovery prochot\n");
				powerlimit_level = 0;
			} else {
				if (power_limit[FUNCTION_SAFETY].spl_mwatt == power_limit[FUNCTION_SLIDER].spl_mwatt) {
					powerlimit_restore = 0;
					return;
				}
				delta = (ABS(battery_current - battery_current_limit_mA)
					* battery_voltage) * 12 / 10 / 1000;
				new_mwatt = new_mwatt + delta;

				power_limit[FUNCTION_SAFETY].spl_mwatt = MIN(new_mwatt, power_limit[FUNCTION_SLIDER].spl_mwatt);
				power_limit[FUNCTION_SAFETY].sppt_mwatt = power_limit[FUNCTION_SAFETY].spl_mwatt;
				power_limit[FUNCTION_SAFETY].fppt_mwatt = power_limit[FUNCTION_SAFETY].spl_mwatt;
				CPRINTF("batt ocp recover, delta: %d, new PL: %d\n",
					delta, power_limit[FUNCTION_SAFETY].spl_mwatt);
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
	static int old_slider_mode = EC_DC_BALANCED;
	int mode = *host_get_memmap(EC_MEMMAP_POWER_SLIDE);
	int active_mpower = charge_manager_get_power_limit_uw() / 1000;
	bool with_dc = ((battery_is_present() == BP_YES) ? true : false );
	int battery_percent = charge_get_percent();

	if (force_no_adapter || (!extpower_is_present())) {
		active_mpower = 0;
		if (mode > EC_DC_BATTERY_SAVER)
			mode = mode << 4;
	}

	if (old_slider_mode != mode) {
		old_slider_mode = mode;
		update_os_power_slider(mode, with_dc, active_mpower);
	}

	update_power_power_limit(battery_percent, active_mpower);

	if (active_mpower == 0)
		update_dc_safety_power_limit();
	else {
		power_limit[FUNCTION_SAFETY].spl_mwatt = 0;
		power_limit[FUNCTION_SAFETY].sppt_mwatt = 0;
		power_limit[FUNCTION_SAFETY].fppt_mwatt = 0;
		power_limit[FUNCTION_SAFETY].p3t_mwatt = 0;
		powerlimit_restore = 0;
	}

	/* choose the lowest one */
	/* use slider as default */
	target_function = FUNCTION_SLIDER; 
	for (int i = FUNCTION_DEFAULT; i < FUNCTION_COUNT; i++) {
		if (power_limit[i].spl_mwatt < 1)
			continue;
		if (power_limit[target_function].spl_mwatt > power_limit[i].spl_mwatt)
			target_function = i;
		else if ((power_limit[target_function].spl_mwatt == power_limit[i].spl_mwatt)
			&& (power_limit[target_function].sppt_mwatt > power_limit[i].sppt_mwatt))
			target_function = i;
	}

	if (power_limit[target_function].spl_mwatt != old_sustain_power_limit
		|| power_limit[target_function].fppt_mwatt != old_fast_ppt_limit
		|| power_limit[target_function].sppt_mwatt != old_slow_ppt_limit
		|| power_limit[target_function].p3t_mwatt != old_p3t_limit
		|| force_update) {
		/* only set PL when it is changed */
		old_sustain_power_limit = power_limit[target_function].spl_mwatt;
		old_fast_ppt_limit = power_limit[target_function].fppt_mwatt;
		old_slow_ppt_limit = power_limit[target_function].sppt_mwatt;
		old_p3t_limit = power_limit[target_function].p3t_mwatt;
		CPRINTF("SOC Power Limit: target %d, SPL %dmW, fPPT %dmW, sPPT %dmW p3T %dmW\n",
			target_function, old_sustain_power_limit, old_fast_ppt_limit, old_slow_ppt_limit, old_p3t_limit);
		set_pl_limits(old_sustain_power_limit, old_fast_ppt_limit, old_slow_ppt_limit, old_p3t_limit);
	}
}

void update_soc_power_limit_hook(void)
{
	if (!manual_ctl)
		update_soc_power_limit(false, false);
}
DECLARE_HOOK(HOOK_SECOND, update_soc_power_limit_hook, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_AC_CHANGE, update_soc_power_limit_hook, HOOK_PRIO_DEFAULT);

static void initial_soc_power_limit(void)
{
	char *str = "FRANGWAT01";
	battery_mwatt_type =
		strncmp(battery_static[BATT_IDX_MAIN].model_ext, str, 10) ? BATTERY_55mW : BATTERY_61mW;
	battery_p3t_mwatt =
		((battery_mwatt_type == BATTERY_55mW) ? 100000 : 90000);
	battery_current_limit_mA =
		((battery_mwatt_type == BATTERY_55mW) ? -3570 : -3920);
	
	/* initial slider table to battery balance as default */
	power_limit[FUNCTION_SLIDER].spl_mwatt = 28000;
	power_limit[FUNCTION_SLIDER].sppt_mwatt = 33000;
	power_limit[FUNCTION_SLIDER].fppt_mwatt =
		((battery_mwatt_type == BATTERY_55mW) ? 35000 : 41000);
	power_limit[FUNCTION_SLIDER].p3t_mwatt = battery_p3t_mwatt - POWER_DELTA - ports_cost;
}
DECLARE_HOOK(HOOK_INIT, initial_soc_power_limit, HOOK_PRIO_INIT_I2C);

static int cmd_cpupower(int argc, const char **argv)
{
	uint32_t spl, fppt, sppt, p3t;
	char *e;

	CPRINTF("SOC Power Limit: function=%d, SPL %dmW, fPPT %dmW, sPPT %dmW, p3T %dmW\n",
		target_function, power_limit[target_function].spl_mwatt, power_limit[target_function].fppt_mwatt,
		power_limit[target_function].sppt_mwatt, power_limit[target_function].p3t_mwatt);

	if (argc >= 2) {
		if (!strncmp(argv[1], "auto", 4)) {
			manual_ctl = false;
			CPRINTF("Auto Control");
			update_soc_power_limit(false, false);
		}
		if (!strncmp(argv[1], "manual", 6)) {
			manual_ctl = true;
			CPRINTF("Manual Control");
		}
		if (!strncmp(argv[1], "table", 5)) {
			CPRINTF("Table Power Limit:\n");
			for (int i = FUNCTION_DEFAULT; i < FUNCTION_COUNT; i++) {
				CPRINTF("function %d, SPL %dmW, fPPT %dmW, sPPT %dmW, p3T %dmW\n",
					i, power_limit[i].spl_mwatt, 
					power_limit[i].fppt_mwatt, power_limit[i].sppt_mwatt,
					power_limit[i].p3t_mwatt);
			}
		}
	}

	if (argc >= 5) {
		spl = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;
		fppt = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		sppt = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM3;

		p3t = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM4;

		set_pl_limits(spl, fppt, sppt, p3t);

	}
	return EC_SUCCESS;

}
DECLARE_CONSOLE_COMMAND(cpupower, cmd_cpupower,
			"cpupower spl fppt sppt p3t (unit mW)",
			"Set/Get the cpupower limit");
