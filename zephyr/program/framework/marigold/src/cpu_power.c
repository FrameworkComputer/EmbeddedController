


#include "charge_state.h"
#include "charger.h"
#include "charge_manager.h"
#include "chipset.h"
#include "common_cpu_power.h"
#include "customized_shared_memory.h"
#include "console.h"
#include "extpower.h"
#include "hooks.h"
#include "math_util.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

void update_soc_power_limit(bool force_update, bool force_no_adapter)
{
	/*
	 * power limit is related to AC state, battery percentage, and power budget
	 */

	int active_power;
	int pps_power_budget = 0;
	int battery_percent;

	static int old_pl2_watt = -1;
	static int old_pl4_watt = -1;
	static int old_psys_watt = -1;

	/* TODO: get the power and pps_power_budget */
	/* pps_power_budget = cypd_get_pps_power_budget(); */
	battery_percent = charge_get_percent();
	active_power = charge_manager_get_power_limit_uw() / 1000000;

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
		pl4_watt = 140;
		/* psys watt = adp watt * 0.95 + battery watt(55 W) * 0.7 - pps power budget */
		psys_watt = ((active_power * 95) / 100) + 39 - pps_power_budget;
	}
	if (pl2_watt != old_pl2_watt || pl4_watt != old_pl4_watt ||
			psys_watt != old_psys_watt || force_update) {
		old_psys_watt = psys_watt;
		old_pl4_watt = pl4_watt;
		old_pl2_watt = pl2_watt;

		pl1_watt = POWER_LIMIT_1_W;
	}
}
