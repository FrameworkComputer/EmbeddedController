


#include "battery_fuel_gauge.h"
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

enum battery_wattage { none, battery_55w, battery_61w };
enum battery_wattage get_battery_wattage(void)
{
	char device_name[32];
	int curr_batt_present = battery_is_present();
	static int pre_batt_present;
	static enum battery_wattage curr_batt_watt;


	if (pre_batt_present != curr_batt_present) {
		/* read the battery device name */
		if (battery_device_name(device_name, sizeof(device_name)))
			curr_batt_watt = none;
		else {
			if (!strncmp(device_name, "Framework Laptop", 16))
				curr_batt_watt = battery_55w;
			else if (!strncmp(device_name, "FRANGWAT01", 10))
				curr_batt_watt = battery_61w;
		}
		pre_batt_present = curr_batt_present;
	}

	return curr_batt_watt;
}

void update_soc_power_limit(bool force_update, bool force_no_adapter)
{
	int active_power;
	int power;
	int battery_percent;
	enum battery_wattage battery_watt;

	static int old_pl2_watt = -1;
	static int old_pl4_watt = -1;
	static bool communication_fail;

	battery_watt = get_battery_wattage();
	battery_percent = charge_get_percent();
	active_power = charge_manager_get_power_limit_uw() / 1000000;

	if (force_no_adapter) {
		active_power = 0;
	}

	if (!extpower_is_present() || (active_power < 55)) {
		/* Battery only or ADP < 55W */
		if (battery_watt == battery_55w) {
			pl2_watt = 35;
			pl4_watt = 50;
		} else if (battery_watt == battery_61w) {
			pl2_watt = 41;
			pl4_watt = 60;
		}

	} else if (battery_percent <= 30) {
		/* ADP >= 55W and Battery percentage <= 30% */
		power = ((active_power * 95) / 100);
		pl2_watt = MIN((power-20), 60);
		pl4_watt = power;

	} else {
		/* ADP >= 55W and Battery percentage > 30% */
		power = ((active_power * 95) / 100) - 20;
		if (battery_watt == battery_55w) {
			pl2_watt = MIN((power + 55), 60);
			pl4_watt = MIN((power + 78), 120);
		} else if (battery_watt == battery_61w) {
			pl2_watt = MIN((power + 61), 60);
			pl4_watt = MIN((power + 87), 120);
		}
	}

	if (pl2_watt != old_pl2_watt || pl4_watt != old_pl4_watt ||
			force_update || communication_fail) {
		old_pl4_watt = pl4_watt;
		old_pl2_watt = pl2_watt;

		pl1_watt = POWER_LIMIT_1_W;
		communication_fail = set_pl_limits(pl1_watt, pl2_watt, pl4_watt);

		if (!communication_fail)
			CPRINTS("PL1:%d, PL2:%d and PL4:%d updated success",
				pl1_watt, pl2_watt, pl4_watt);
	}
}
