


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
bool manual_ctl;
static int battery_mwatt_type;
static int battery_mwatt_p3t;
static int battery_current_limit_mA;
static int target_func[TYPE_COUNT];
static int powerlimit_restore;

static int update_sustained_power_limit(uint32_t mwatt)
{
	uint32_t msgIn = 0;
	uint32_t msgOut;

	msgIn = mwatt;

	return sb_rmi_mailbox_xfer(SB_RMI_WRITE_SUSTAINED_POWER_LIMIT_CMD, msgIn, &msgOut);
}

static int update_fast_ppt_limit(uint32_t mwatt)
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
	update_fast_ppt_limit(fppt);
	update_slow_ppt_limit(sppt);
	update_peak_package_power_limit(p3t);
}

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

static void update_adapter_power_limit(int battery_percent,
	int active_mpower, bool with_dc, int ports_cost)
{
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
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_POWER_LIMIT_EVENT) |= CPB_DISABLE;
	} else if ((battery_percent < 30) && (active_mpower >= 55000)) {
		/* AC (With Battery) (Battery Capacity < 30%, ADP >= 55W) */
		power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 30000;
		power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = (active_mpower * 85 / 100) - 20000;
		power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = (active_mpower * 85 / 100) - 15000;
		power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
			(active_mpower * 95 / 100) - 15000 + battery_mwatt_type;
		/* CPB enable */
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_POWER_LIMIT_EVENT) &= ~CPB_DISABLE;
	} else if ((battery_percent >= 30) && (active_mpower >= 45000)) {
		/* AC (With Battery) (Battery Capacity >= 30%, ADP >= 45W) */
		power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 30000;
		power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 35000;
		power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = 53000;
		power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
			(active_mpower * 95 / 100) - 15000 + battery_mwatt_type;
		/* CPB enable */
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_POWER_LIMIT_EVENT) &= ~CPB_DISABLE;
	} else {
		/* otherwise, take as DC only case */
		power_limit[FUNCTION_POWER].mwatt[TYPE_SPL] = 30000;
		power_limit[FUNCTION_POWER].mwatt[TYPE_SPPT] = 35000;
		power_limit[FUNCTION_POWER].mwatt[TYPE_FPPT] = battery_mwatt_type - 15000;
		/* DC mode p3t should follow os_power_slider */
		power_limit[FUNCTION_POWER].mwatt[TYPE_P3T] =
			power_limit[FUNCTION_SLIDER].mwatt[TYPE_P3T];
		/* CPB enable */
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_POWER_LIMIT_EVENT) &= ~CPB_DISABLE;
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
	static int old_slider_mode = EC_DC_BALANCED;
	int mode = *host_get_memmap(EC_MEMMAP_POWER_SLIDE);
	int active_mpower = charge_manager_get_power_limit_uw() / 1000;
	bool with_dc = ((battery_is_present() == BP_YES) ? true : false);
	int battery_percent = charge_get_percent();
	int ports_cost;

	ports_cost = cypd_get_port_cost();

	/* azalea take 55W and lower adp as no ac */
	if (force_no_adapter || (!extpower_is_present()) || (active_mpower < 55000)) {
		active_mpower = 0;
		if (mode > EC_DC_BATTERY_SAVER)
			mode = mode << 4;
	}

	if (old_slider_mode != mode) {
		old_slider_mode = mode;
		update_os_power_slider(mode, active_mpower);
	}

	update_adapter_power_limit(battery_percent, active_mpower, with_dc, ports_cost);

	if (active_mpower == 0)
		update_dc_safety_power_limit();
	else {
		power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPL] = 0;
		power_limit[FUNCTION_SAFETY].mwatt[TYPE_SPPT] = 0;
		power_limit[FUNCTION_SAFETY].mwatt[TYPE_FPPT] = 0;
		power_limit[FUNCTION_SAFETY].mwatt[TYPE_P3T] = 0;
		powerlimit_restore = 0;
	}

	/* when trigger thermal warm, reduce SPL to 15W */
	if (thermal_warn_trigger())
		power_limit[FUNCTION_THERMAL].mwatt[TYPE_SPPT] = 15000;
	else
		power_limit[FUNCTION_THERMAL].mwatt[TYPE_SPPT] = 0;

	/* choose the lowest one */
	for (int item = TYPE_SPL; item < TYPE_P3T; item++) {
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
		|| force_update) {
		/* only set PL when it is changed */
		old_sustain_power_limit = power_limit[target_func[TYPE_SPL]].mwatt[TYPE_SPL];
		old_slow_ppt_limit = power_limit[target_func[TYPE_SPPT]].mwatt[TYPE_SPPT];
		old_fast_ppt_limit = power_limit[target_func[TYPE_FPPT]].mwatt[TYPE_FPPT];
		old_p3t_limit = power_limit[target_func[TYPE_P3T]].mwatt[TYPE_P3T];

		CPRINTF("Change SOC Power Limit: SPL %dmW, sPPT %dmW, fPPT %dmW p3T %dmW\n",
			old_sustain_power_limit, old_slow_ppt_limit,
			old_fast_ppt_limit, old_p3t_limit);
		set_pl_limits(old_sustain_power_limit, old_fast_ppt_limit,
			old_slow_ppt_limit, old_p3t_limit);
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
		(strncmp(battery_static[BATT_IDX_MAIN].model_ext, str, 10) ?
		BATTERY_55mW : BATTERY_61mW);
	battery_mwatt_p3t =
		((battery_mwatt_type == BATTERY_61mW) ? 90000 : 100000);
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
DECLARE_HOOK(HOOK_INIT, initial_soc_power_limit, HOOK_PRIO_INIT_I2C);

static int cmd_cpupower(int argc, const char **argv)
{
	uint32_t spl, fppt, sppt, p3t;
	char *e;

	CPRINTF("Now SOC Power Limit:\n FUNC = %d, SPL %dmW,\n",
		target_func[TYPE_SPL], power_limit[target_func[TYPE_SPL]].mwatt[TYPE_SPL]);
	CPRINTF("FUNC = %d, fPPT %dmW,\n FUNC = %d, sPPT %dmW,\n FUNC = %d, p3T %dmW\n",
		target_func[TYPE_FPPT], power_limit[target_func[TYPE_FPPT]].mwatt[TYPE_FPPT],
		target_func[TYPE_SPPT], power_limit[target_func[TYPE_SPPT]].mwatt[TYPE_SPPT],
		target_func[TYPE_P3T], power_limit[target_func[TYPE_P3T]].mwatt[TYPE_P3T]);

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
					i, power_limit[i].mwatt[TYPE_SPL],
					power_limit[i].mwatt[TYPE_FPPT],
					power_limit[i].mwatt[TYPE_SPPT],
					power_limit[i].mwatt[TYPE_P3T]);
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
