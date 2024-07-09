


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

#define ROP 15
#define batt_rating 61

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
	int battery_percent;
	enum battery_wattage battery_watt;

	static int old_pl1_watt = -1;
	static int old_pl2_watt = -1;
	static int old_pl4_watt = -1;
	static int old_psyspl2_watt = -1;
	static bool communication_fail;

	battery_watt = get_battery_wattage();
	battery_percent = charge_get_percent();
	active_power = charge_manager_get_power_limit_uw() / 1000000;

	if (force_no_adapter) {
		active_power = 0;
	}

	if (!extpower_is_present()  || active_power == 0) {
		/* Battery only, same for 61wh and 55wh battery */
		pl1_watt = 28;
		pl2_watt = batt_rating - ROP;
		pl4_watt = 80;
		psyspl2_watt = (batt_rating * 95) / 100;
	} else if (battery_watt == none && active_power >= 60) {
		/*Standalone mode AC only and AC >= 60W*/
		pl1_watt = 30;
		pl2_watt = 40;
		pl4_watt = ((active_power * 95) / 100);
		psyspl2_watt = ((active_power * 95) / 100);
	} else if (battery_percent >= 30 && active_power >= 55) {
		/* ADP >= 55W and Battery percentage >= 30% */
		pl1_watt = 30;
		pl2_watt = 60;
		pl4_watt = 120;
		psyspl2_watt = ((active_power * 95) / 100) + ((batt_rating * 70) / 100);
	} else if (battery_percent < 30 && active_power >= 55) {
		/* ADP >= 55W and Battery percentage < 30% */
		pl1_watt = 30;
		pl2_watt = MIN(((active_power * 90) / 100) - ROP, 60);
		pl4_watt = MIN(((active_power * 90) / 100) + 80, 120);
		psyspl2_watt = ((active_power * 95) / 100);
	} else {
		/*AC+DC and AC < 55W*/
		pl1_watt = 28;
		pl2_watt = batt_rating - ROP;
		pl4_watt = 80;
		psyspl2_watt = ((batt_rating * 95) / 100);
	}

	if (pl1_watt != old_pl1_watt || pl2_watt != old_pl2_watt || pl4_watt != old_pl4_watt ||
			psyspl2_watt != old_psyspl2_watt || force_update || communication_fail) {
		old_pl1_watt = pl1_watt;
		old_pl2_watt = pl2_watt;
		old_pl4_watt = pl4_watt;
		old_psyspl2_watt = psyspl2_watt;

		communication_fail = set_pl_limits(pl1_watt, pl2_watt, pl4_watt, psyspl2_watt);

		if (!communication_fail)
			CPRINTS("PL1:%d, PL2:%d, PL4:%d, PSYSPL2:%d updated success",
				pl1_watt, pl2_watt, pl4_watt, psyspl2_watt);
	}
}
