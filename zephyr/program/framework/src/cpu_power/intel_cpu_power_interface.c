/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

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
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

int pl1_watt;
int pl2_watt;
int pl4_watt;
int psys_watt;
bool manual_ctl;


/* TODO implement peci interface*/
void set_pl_limits(int pl1, int pl2, int pl4, int psys)
{
	/* peci_update_PL1(pl1); */
	/* peci_update_PL2(pl2); */
	/* peci_update_PL4(pl4); */
	/* peci_update_PsysPL2(psys); */
}

void update_soc_power_limit_hook(void)
{
	if (!manual_ctl)
		update_soc_power_limit(false, false);
}

DECLARE_HOOK(HOOK_AC_CHANGE, update_soc_power_limit_hook, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, update_soc_power_limit_hook, HOOK_PRIO_DEFAULT);

void update_soc_power_on_boot_deferred(void)
{
	if (!manual_ctl)
		update_soc_power_limit(true, false);
}
DECLARE_DEFERRED(update_soc_power_on_boot_deferred);

void update_soc_power_limit_boot(void)
{
	hook_call_deferred(&update_soc_power_on_boot_deferred_data, MSEC*1000);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, update_soc_power_limit_boot, HOOK_PRIO_DEFAULT);

static int cmd_cpupower(int argc, const char **argv)
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
