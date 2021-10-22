


#include "charge_state.h"
#include "charger.h"
#include "charge_manager.h"
#include "chipset.h"
#include "console.h"
#include "extpower.h"
#include "hooks.h"
#include "host_command.h"
#include "peci.h"
#include "peci_customization.h"
#include "cypress5525.h"
#include "math_util.h"
#include "util.h"


#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

#define POWER_LIMIT_1_W	28

static int pl1_watt;
static int pl2_watt;
static int pl4_watt;
static int psys_watt;
bool manual_ctl;

void set_pl_limits(int pl1, int pl2, int pl4, int psys)
{
		peci_update_PL1(pl1);
		peci_update_PL2(pl2);
		peci_update_PL4(pl4);
		peci_update_PsysPL2(psys);
}

void update_soc_power_limit(bool force_update, bool force_no_adapter)
{
	/*
	 * power limit is related to AC state, battery percentage, and power budget
	 */

	int active_power;
	int pps_power_budget;
	int battery_percent;

	static int old_pl2_watt = -1;
	static int old_pl4_watt = -1;
	static int old_psys_watt = -1;

	/* TODO: get the power and pps_power_budget */
	battery_percent = charge_get_percent();
	active_power = charge_manager_get_power_limit_uw()/1000000;
	pps_power_budget = cypd_get_pps_power_budget();

	if (force_no_adapter) {
		active_power = 0;
	}

	if (!extpower_is_present() || (active_power < 55)) {
		/* Battery only or ADP < 55W */
		pl2_watt = POWER_LIMIT_1_W;
		pl4_watt = 70 - pps_power_budget;
		psys_watt = 52 - pps_power_budget;
	} else if (battery_percent < 30) {
		/* ADP > 55W and Battery percentage < 30% */
		pl4_watt = active_power - 15 - pps_power_budget;
		pl2_watt = MIN((pl4_watt * 90) / 100, 64);
		psys_watt = ((active_power * 95) / 100) - pps_power_budget;
	} else {
		/* ADP > 55W and Battery percentage >= 30% */
		pl2_watt = 64;
		pl4_watt = 121;
		/* psys watt = adp watt * 0.95 + battery watt(55 W) * 0.7 - pps power budget */
		psys_watt = ((active_power * 95) / 100) + 39 - pps_power_budget;
	}
	if (pl2_watt != old_pl2_watt || pl4_watt != old_pl4_watt ||
			psys_watt != old_psys_watt || force_update) {
		old_psys_watt = psys_watt;
		old_pl4_watt = pl4_watt;
		old_pl2_watt = pl2_watt;

		pl1_watt = POWER_LIMIT_1_W;
		if (manual_ctl == false) {
			CPRINTS("Updating SOC Power Limits: PL2 %d, PL4 %d, Psys %d, Adapter %d",
				pl2_watt, pl4_watt, psys_watt, active_power);
			set_pl_limits(pl1_watt, pl2_watt, pl4_watt, psys_watt);
		}
	}
}

void update_soc_power_limit_hook(void)
{
	update_soc_power_limit(false, false);
}

DECLARE_HOOK(HOOK_AC_CHANGE, update_soc_power_limit_hook, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, update_soc_power_limit_hook, HOOK_PRIO_DEFAULT);



static int cmd_cpupower(int argc, char **argv)
{
	uint32_t pl1, pl2, pl4, psys;
	char *e;

	CPRINTF("SOC Power Limit: PL1 %d, PL2 %d, PL4 %d, Psys %d\n",
		pl1_watt, pl2_watt, pl4_watt, psys_watt);
	if (argc >= 2) {
		if (!strncmp(argv[1], "auto", 4)) {
			manual_ctl = false;
			CPRINTF("Auto Control");
			update_soc_power_limit(false, false);
		}
		if (!strncmp(argv[1], "manual", 6)) {
			manual_ctl = true;
			CPRINTF("Manual Control");
			set_pl_limits(pl1_watt, pl2_watt, pl4_watt, psys_watt);
		}
	}

	if (argc >= 5) {
		pl1 = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;
		pl2 = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;
		pl4 = strtoi(argv[3], &e, 0);
		if (*e)
			return EC_ERROR_PARAM3;
		psys = strtoi(argv[4], &e, 0);
		if (*e)
			return EC_ERROR_PARAM4;
		pl1_watt = pl1;
		pl2_watt = pl2;
		pl4_watt = pl4;
		psys_watt = psys;
		set_pl_limits(pl1_watt, pl2_watt, pl4_watt, psys_watt);

	}
	return EC_SUCCESS;

}
DECLARE_CONSOLE_COMMAND(cpupower, cmd_cpupower,
			"cpupower pl1 pl2 pl4 psys ",
			"Set/Get the cpupower limit");
