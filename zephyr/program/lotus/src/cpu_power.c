


#include "charge_state.h"
#include "charger.h"
#include "charge_manager.h"
#include "chipset.h"
#include "cpu_power.h"
#include "console.h"
#include "driver/sb_rmi.h"
#include "extpower.h"
#include "hooks.h"
#include "math_util.h"
#include "util.h"


#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

static uint32_t spl_watt;
static uint32_t sppt_watt;
static uint32_t fppt_watt;
bool manual_ctl;

static int update_sustained_power_limit(int32_t mwatt)
{
	uint32_t msgIn = 0;
	uint32_t msgOut;

	msgIn = mwatt;

	return sb_rmi_mailbox_xfer(SB_RMI_WRITE_SUSTAINED_POWER_LIMIT_CMD, msgIn, &msgOut);
}

static int update_flow_ppt_limit(int32_t mwatt)
{
	uint32_t msgIn = 0;
	uint32_t msgOut;

	msgIn = mwatt;

	return sb_rmi_mailbox_xfer(SB_RMI_WRITE_FAST_PPT_LIMIT_CMD, msgIn, &msgOut);
}

static int update_slow_ppt_limit(int32_t mwatt)
{
	uint32_t msgIn = 0;
	uint32_t msgOut;

	msgIn = mwatt;

	return sb_rmi_mailbox_xfer(SB_RMI_WRITE_SLOW_PPT_LIMIT_CMD, msgIn, &msgOut);
}

static void set_pl_limits(int32_t spl, int32_t fppt, int32_t sppt)
{
	update_sustained_power_limit(spl);
	update_flow_ppt_limit(fppt);
	update_slow_ppt_limit(sppt);
}

void update_soc_power_limit(bool force_update, bool force_no_adapter)
{
	/*
	 * power limit is related to AC state, battery percentage, and power budget
	 */

	uint32_t active_mpower;
	uint32_t temp_power;
	int battery_percent;

	static uint32_t old_sustain_power_limit = -1;
	static uint32_t old_fast_ppt_limit = -1;
	static uint32_t old_slow_ppt_limit = -1;

	battery_percent = charge_get_percent();
	active_mpower = charge_manager_get_power_limit_uw() / 1000;

	if (force_no_adapter) {
		active_mpower = 0;
	}

	/* TODO: update the P3T value */

	if (battery_is_present()) {
		/* Battery is present */
		spl_watt = 45000;
		fppt_watt = 54000;
		sppt_watt = 65000;
	} else {
		if (active_mpower <= 100000) {
			spl_watt = 45000;
			temp_power = active_mpower * 88 / 100;
			if (temp_power < spl_watt)
				temp_power = spl_watt;

			fppt_watt = temp_power;
			sppt_watt = temp_power;
		} else {
			spl_watt = 45000;
			temp_power = active_mpower * 88 * 90 / 10000;
			if (temp_power < spl_watt)
				temp_power = spl_watt;

			fppt_watt = temp_power;
			sppt_watt = temp_power;
		}
	}

	if (spl_watt != old_sustain_power_limit || fppt_watt != old_fast_ppt_limit ||
			sppt_watt != old_slow_ppt_limit || force_update) {
		old_sustain_power_limit = spl_watt;
		old_fast_ppt_limit = fppt_watt;
		old_slow_ppt_limit = sppt_watt;

		if (manual_ctl == false) {
			CPRINTF("SOC Power Limit: SPL %dmW, fPPT %dmW, sPPT %dmW\n",
				spl_watt, fppt_watt, sppt_watt);

			set_pl_limits(spl_watt, fppt_watt, sppt_watt);
		}
	}
}

void update_soc_power_limit_hook(void)
{
	update_soc_power_limit(false, false);
}

DECLARE_HOOK(HOOK_AC_CHANGE, update_soc_power_limit_hook, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, update_soc_power_limit_hook, HOOK_PRIO_DEFAULT);



void update_soc_power_on_boot_deferred(void)
{
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
	uint32_t spl, fppt, sppt;
	char *e;

	CPRINTF("SOC Power Limit: SPL %dmW, fPPT %dmW, sPPT %dmW\n",
		spl_watt, fppt_watt, sppt_watt);

	if (argc >= 2) {
		if (!strncmp(argv[1], "auto", 4)) {
			manual_ctl = false;
			CPRINTF("Auto Control");
			update_soc_power_limit(false, false);
		}
		if (!strncmp(argv[1], "manual", 6)) {
			manual_ctl = true;
			CPRINTF("Manual Control");
			set_pl_limits(spl_watt, fppt_watt, sppt_watt);
		}
	}

	if (argc >= 4) {
		spl = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;
		fppt = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		sppt = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		spl_watt = spl;
		fppt_watt = fppt;
		sppt_watt = sppt;

		set_pl_limits(spl_watt, fppt_watt, sppt_watt);

	}
	return EC_SUCCESS;

}
DECLARE_CONSOLE_COMMAND(cpupower, cmd_cpupower,
			"cpupower fppt sppt ",
			"Set/Get the cpupower limit");
