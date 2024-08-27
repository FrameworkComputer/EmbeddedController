/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

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

#define ROP 6
#define batt_rating 50

void update_soc_power_limit(bool force_update, bool force_no_adapter)
{
	int active_power;
	int battery_percent;

	static int old_pl1_watt = -1;
	static int old_pl2_watt = -1;
	static int old_pl4_watt = -1;
	static int old_psyspl2_watt = -1;
	static bool communication_fail;

	battery_percent = charge_get_percent();
	active_power = charge_manager_get_power_limit_uw() / 1000000;

	if (force_no_adapter) {
		active_power = 0;
	}

	if (!extpower_is_present()  || active_power == 0) {
		/* Battery only, 50wh battery */
		pl1_watt = 15;
		pl2_watt = batt_rating - ROP;
		pl4_watt = (batt_rating * 13) / 10;
		psyspl2_watt = (batt_rating * 95) / 100;
	} else if (!battery_is_present() && active_power >= 60) {
		/*Standalone mode AC only and AC >= 60W*/
		pl1_watt = 15;
		pl2_watt = 40;
		pl4_watt = ((active_power * 95) / 100);
		psyspl2_watt = ((active_power * 95) / 100);
	} else if (battery_percent >= 30 && active_power >= 55) {
		/* Battery percentage >= 30% and ADP >= 55W */
		pl1_watt = 15;
		pl2_watt = 40;
		pl4_watt = 87;
		psyspl2_watt = ((active_power * 95) / 100) + ((batt_rating * 70) / 100);
	} else if (battery_percent < 30 && active_power >= 55) {
		/* Battery percentage < 30% and ADP >= 55W */
		pl1_watt = 15;
		pl2_watt = MIN(((active_power * 90) / 100) - ROP, 40);
		pl4_watt = MIN(((active_power * 90) / 100) + ((batt_rating * 13) / 10), 87);
		psyspl2_watt = ((active_power * 95) / 100);
	} else {
		/* AC+DC and AC < 55W */
		pl1_watt = 15;
		pl2_watt = batt_rating - ROP;
		pl4_watt = ((batt_rating * 13) / 10);
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
