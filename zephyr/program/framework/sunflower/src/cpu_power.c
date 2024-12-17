/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery_fuel_gauge.h"
#include "board_battery.h"
#include "charge_state.h"
#include "charger.h"
#include "charge_manager.h"
#include "chipset.h"
#include "common_cpu_power.h"
#include "customized_shared_memory.h"
#include "console.h"
#include "cypress_pd_common.h"
#include "extpower.h"
#include "hooks.h"
#include "math_util.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

#define ROP 15   /*Intel Rest Of Platform(ROP)*/
#define batt_rating 50

/*
 * During the safety test in DC mode, the system's wattage exceeds the battery
 * discharge wattage, resulting in an inability to limit current. Reduce the
 * type-c port from 3A down to 1.5A when the battery current limit is exceeded,
 * the test result could pass the safety test criteria.
 */
static void reduce_typec_1_5A(void)
{
	int average_current = get_average_battery_current();
	static int battery_current_limit_mA = -4265;
	static timestamp_t wait_stable_time;
	static timestamp_t update_safety_timer;

	static int pd_3a_controller;
	static int pd_3a_port;
	static int set_typec_1_5a_flag;
	int rv;

	timestamp_t now = get_time();

	if (!timestamp_expired(wait_stable_time, &now) ||
		!timestamp_expired(update_safety_timer, &now))
		return;

	/* discharge, value compare based on negative */
	if (average_current < battery_current_limit_mA) {
		if (set_typec_1_5a_flag == 0) {
			for (int controller = 0; controller < PD_CHIP_COUNT; controller++) {
				for (int port_idx = 0; port_idx < 2; port_idx++) {
					if (cypd_port_3a_status(controller, port_idx)) {
						pd_3a_controller = controller;
						pd_3a_port = port_idx;
						rv = cypd_modify_safety_power(pd_3a_controller,
							pd_3a_port, CCG_PD_CMD_SET_TYPEC_1_5A);
						set_typec_1_5a_flag = 1;
					}
				}
			}
		}
	} else if (average_current > (battery_current_limit_mA * 80 / 100)) {
		if (set_typec_1_5a_flag) {
			rv = cypd_modify_safety_power(pd_3a_controller,
					pd_3a_port, CCG_PD_CMD_SET_TYPEC_3A);
			set_typec_1_5a_flag = 0;
		}
	}

	/* only check safety function per 2 second */
	update_safety_timer.val = get_time().val + (2 * SECOND);
}

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
		reduce_typec_1_5A();
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
