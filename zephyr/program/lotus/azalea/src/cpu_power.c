


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

static uint32_t spl_mwatt;
static uint32_t sppt_mwatt;
static uint32_t fppt_mwatt;
static uint32_t p3t_mwatt;
static int battery_init;
static struct power_limit_details power_limit[FUNCTION_COUNT];
bool manual_ctl;

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

/* set 1 for first boot setting */
int old_slider_mode = 1;

void update_os_power_slider(void)
{
	int mode = *host_get_memmap(EC_MEMMAP_POWER_SLIDE);
	uint32_t active_mpower = charge_manager_get_power_limit_uw() / 1000;
	bool ac_with_dc = ((battery_is_present() == BP_YES) ? true : false );
	char *str = "FRANGWAT01";

	int battery_fppt =
		(strncmp(battery_static[BATT_IDX_MAIN].model_ext, str, 10) ? 35000 : 41000);

	if (old_slider_mode == mode)
		return;

	old_slider_mode = mode;

	switch (mode) {
	case EC_DC_BEST_PERFORMANCE:
		power_limit[FUNCTION_SLIDER].spl_mwatt = 30000;
		power_limit[FUNCTION_SLIDER].sppt_mwatt = 35000;
		power_limit[FUNCTION_SLIDER].fppt_mwatt = battery_fppt;
		CPRINTS("DC BEST PERFORMANCE");
		break;
	case EC_DC_BALANCED:
		power_limit[FUNCTION_SLIDER].spl_mwatt = 28000;
		power_limit[FUNCTION_SLIDER].sppt_mwatt = 33000;
		power_limit[FUNCTION_SLIDER].fppt_mwatt = battery_fppt;
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
		power_limit[FUNCTION_SLIDER].sppt_mwatt = (ac_with_dc ? 35000 : (MIN(35000, ((active_mpower - 15000) * 9 / 10))));
		power_limit[FUNCTION_SLIDER].fppt_mwatt = 53000;
		CPRINTS("AC BEST PERFORMANCE");
		break;
	case EC_AC_BALANCED:
		power_limit[FUNCTION_SLIDER].spl_mwatt = 28000;
		power_limit[FUNCTION_SLIDER].sppt_mwatt = (ac_with_dc ? 33000 : (MIN(33000, ((active_mpower - 15000) * 9 / 10))));
		power_limit[FUNCTION_SLIDER].fppt_mwatt = (ac_with_dc ? 51000 : (MIN(51000, (active_mpower - 15000))));
		CPRINTS("AC BALANCED");
		break;
	case EC_AC_BEST_EFFICIENCYE:
		power_limit[FUNCTION_SLIDER].spl_mwatt = (ac_with_dc ? 15000 : 28000);
		power_limit[FUNCTION_SLIDER].sppt_mwatt = (ac_with_dc ? 25000 : (MIN(33000, ((active_mpower - 15000) * 9 / 10))));
		power_limit[FUNCTION_SLIDER].fppt_mwatt = (ac_with_dc ? 30000 : (MIN(51000, (active_mpower - 15000))));
		CPRINTS("AC BEST EFFICIENCYE");
		break;
	default:
		/* no mode, run power table */
		break;
	}
	update_soc_power_limit(false, false);
}
DECLARE_HOOK(HOOK_TICK, update_os_power_slider, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_INIT, update_os_power_slider, HOOK_PRIO_DEFAULT);

void update_soc_power_limit(bool force_update, bool force_no_adapter)
{
	/*
	 * power limit is related to AC state, battery percentage, and power budget
	 */

	uint32_t active_mpower;
	int battery_percent;
	int battery_current_limit_mA;
	char *str = "FRANGWAT01";
	int battery_current;
	int battery_voltage;
	int new_mwatt;
	int delta;

	static uint32_t old_sustain_power_limit = -1;
	static uint32_t old_fast_ppt_limit = -1;
	static uint32_t old_slow_ppt_limit = -1;
	static uint32_t old_p3t_limit = -1;
	static int powerlimit_level;

	const struct batt_params *batt = charger_current_battery_params();

	battery_percent = charge_get_percent();
	active_mpower = charge_manager_get_power_limit_uw() / 1000;
	battery_current_limit_mA =
		(strncmp(battery_static[BATT_IDX_MAIN].model_ext, str, 10) ? -3570 : -3920);
	CPRINTF("START SOC_PL, batt limit is %d\n", battery_current_limit_mA);


	battery_current = batt->current;
	battery_voltage = battery_dynamic[BATT_IDX_MAIN].actual_voltage;

	if (force_no_adapter) {
		active_mpower = 0;
	}

	if (!extpower_is_present()) {
		/* Battery is present */
		CPRINTF("Monitor batt curr= %d, trigger_prochot = %d\n",
			battery_current, powerlimit_level);
		if (!battery_init) {
			spl_mwatt = 28000;
			sppt_mwatt = 28000;
			fppt_mwatt = 28000;
			p3t_mwatt = 65000;
			battery_init = 1;
		} else {
			new_mwatt = spl_mwatt;
			/* start tuning PL by formate */
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

				spl_mwatt = MAX(new_mwatt, 15000);
				sppt_mwatt = spl_mwatt;
				fppt_mwatt = spl_mwatt;
				CPRINTF("batt ocp, delta: %d, new PL: %d\n", delta, spl_mwatt);

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
					delta = (ABS(battery_current - battery_current_limit_mA)
						* battery_voltage) * 12 / 10 / 1000;
					new_mwatt = new_mwatt + delta;

					spl_mwatt = MIN(new_mwatt, 28000);
					sppt_mwatt = spl_mwatt;
					fppt_mwatt = spl_mwatt;
					CPRINTF("batt ocp recover, delta: %d, new PL: %d\n",
						delta, spl_mwatt);
				}
			}
		}
	} else if (battery_percent < 40) {
		/* ADP > 55W and Battery percentage < 40% */
		spl_mwatt = 30000;
		sppt_mwatt = 35000;
		fppt_mwatt = 53000;
		p3t_mwatt = (active_mpower * 110 * 90 / 10000) + 85000 - 20000;
		battery_init = 0;
	} else {
		/* ADP > 55W and Battery percentage >= 40% */
		spl_mwatt = 30000;
		sppt_mwatt = 35000;
		fppt_mwatt = 53000;
		p3t_mwatt = (active_mpower * 110 / 100) + 85000 - 20000;
		battery_init = 0;
	}

	if (spl_mwatt != old_sustain_power_limit || fppt_mwatt != old_fast_ppt_limit ||
			sppt_mwatt != old_slow_ppt_limit || p3t_mwatt != old_p3t_limit ||
			force_update) {
		old_sustain_power_limit = spl_mwatt;
		old_fast_ppt_limit = fppt_mwatt;
		old_slow_ppt_limit = sppt_mwatt;
		old_p3t_limit = p3t_mwatt;

		if (manual_ctl == false) {
			CPRINTF("SOC Power Limit: SPL %dmW, fPPT %dmW, sPPT %dmW p3T %dmW\n",
				spl_mwatt, fppt_mwatt, sppt_mwatt, p3t_mwatt);

			set_pl_limits(spl_mwatt, fppt_mwatt, sppt_mwatt, p3t_mwatt);
		}
	}
}

void update_soc_power_limit_hook(void)
{
	update_soc_power_limit(false, false);
}
DECLARE_HOOK(HOOK_SECOND, update_soc_power_limit_hook, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_AC_CHANGE, update_soc_power_limit_hook, HOOK_PRIO_DEFAULT);

static int cmd_cpupower(int argc, const char **argv)
{
	uint32_t spl, fppt, sppt, p3t;
	char *e;

	CPRINTF("SOC Power Limit: SPL %dmW, fPPT %dmW, sPPT %dmW, p3T %dmW\n",
		spl_mwatt, fppt_mwatt, sppt_mwatt, p3t_mwatt);

	if (argc >= 2) {
		if (!strncmp(argv[1], "auto", 4)) {
			manual_ctl = false;
			CPRINTF("Auto Control");
			update_soc_power_limit(false, false);
		}
		if (!strncmp(argv[1], "manual", 6)) {
			manual_ctl = true;
			CPRINTF("Manual Control");
			set_pl_limits(spl_mwatt, fppt_mwatt, sppt_mwatt, p3t_mwatt);
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

		spl_mwatt = spl;
		fppt_mwatt = fppt;
		sppt_mwatt = sppt;
		p3t_mwatt = p3t;

		set_pl_limits(spl_mwatt, fppt_mwatt, sppt_mwatt, p3t_mwatt);

	}
	return EC_SUCCESS;

}
DECLARE_CONSOLE_COMMAND(cpupower, cmd_cpupower,
			"cpupower spl fppt sppt p3t (unit mW)",
			"Set/Get the cpupower limit");
