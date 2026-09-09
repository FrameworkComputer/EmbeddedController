/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery_fuel_gauge.h"
#include "board_function.h"
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
	static int old_pl1_watt = -1;
	static int old_pl2_watt = -1;
	static int old_pl4_watt = -1;
	static int old_psyspl2_watt = -1;
	static bool communication_fail;

	int batt_type = board_get_battery_type();
	int active_power = charge_manager_get_power_limit_uw() / 1000000;
	enum battery_present batt_status = battery_is_present();
	int battery_percent = get_system_percentage();

	if (force_no_adapter) {
		active_power = 0;
	}

	if ((!extpower_is_present()  || active_power == 0)) {
		/* DC mode Battery only */
		pl1_watt = 35;
		pl2_watt = 60;
		pl4_watt = 82;

		if (battery_percent > 305) {
			/* DC mode RSOC > 30% */
			if (batt_type == FWK_BATT_ATC_75W) {
				pl4_watt = 105;
				psyspl2_watt = 84;
			} else if (batt_type == FWK_BATT_NVT_61W) {
				psyspl2_watt = 80;
			} else {
				psyspl2_watt = 72;
			}
		} else {
			if (batt_type == FWK_BATT_ATC_75W) {
				pl4_watt = 105;
				psyspl2_watt = 73;
			} else if (batt_type == FWK_BATT_NVT_61W) {
				pl2_watt = 59;
				psyspl2_watt = 71;
			} else {
				pl2_watt = 53;
				psyspl2_watt = 64;
			}
		}

	} else if (batt_status == BP_NO) {
		/*Standalone mode AC only, ERS does not clearly define ADP wattage*/
		pl1_watt = 35;
		pl2_watt = MIN(60, (((active_power * 90 * 90) / 10000) - 5));
		pl4_watt = ((active_power * 110 * 90) / 10000) - 5;
		psyspl2_watt = ((active_power * 90) / 100);

	} else {
		/* AC DC mode */
		pl1_watt = 35;
		pl2_watt = 60;

		if (active_power >= 60 || (battery_percent >= 945 && active_power < 60)) {
			/* ADP >= 60w or RSOC >95% & ADP < 60w */
			pl4_watt = MIN(163, 82 + ((active_power * 110 * 88) / 10000));

			if (batt_type == FWK_BATT_ATC_75W) {
				pl4_watt = MIN(163, 105 + ((active_power * 110 * 88) / 10000));
				psyspl2_watt = MIN(140, 73 + (active_power * 95 / 100));
			} else if (batt_type == FWK_BATT_NVT_61W) {
				psyspl2_watt = MIN(140, 71 + (active_power * 95 / 100));
			} else {
				psyspl2_watt = MIN(140, 64 + (active_power * 95 / 100));
			}
		} else {
			pl4_watt = 82;

			if (battery_percent > 305) {
				/* DC mode RSOC > 30% */
				if (batt_type == FWK_BATT_ATC_75W) {
					pl4_watt = 105;
					psyspl2_watt = 84;
				} else if (batt_type == FWK_BATT_NVT_61W) {
					psyspl2_watt = 80;
				} else {
					psyspl2_watt = 72;
				}
			} else {
				if (batt_type == FWK_BATT_ATC_75W) {
					pl4_watt = 105;
					psyspl2_watt = 73;
				} else if (batt_type == FWK_BATT_NVT_61W) {
					psyspl2_watt = 71;
				} else {
					psyspl2_watt = 64;
				}
			}
		}
	}

	if (pl1_watt != old_pl1_watt || pl2_watt != old_pl2_watt || pl4_watt != old_pl4_watt ||
			psyspl2_watt != old_psyspl2_watt || force_update || communication_fail ||
			power_limit_get_events()) {
		old_pl1_watt = pl1_watt;
		old_pl2_watt = pl2_watt;
		old_pl4_watt = pl4_watt;
		old_psyspl2_watt = psyspl2_watt;

		communication_fail = set_pl_limits(pl1_watt, pl2_watt, pl4_watt, psyspl2_watt);

		if (!communication_fail) {
			power_limit_clear_prochot(PROCHOT_CLEAR_REASON_SUCCESS);
			CPRINTS("PL1:%d, PL2:%d, PL4:%d, PSYSPL2:%d updated success",
				pl1_watt, pl2_watt, pl4_watt, psyspl2_watt);
		}
	}
}
