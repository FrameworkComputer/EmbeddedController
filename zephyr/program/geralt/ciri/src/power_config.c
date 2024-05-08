/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "hooks.h"
#include "power.h"
#include "temp_sensor/temp_sensor.h"
#include "usb_pd.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

#define CHARGE_CURRENT_LIMIT_LEVEL1 2000
#define CHARGE_CURRENT_LIMIT_LEVEL2 400
#define THERMAL_UP_DELAY 2

static int current_level;
static int current_level_pre;
static int typec_snk_status;
static int thermal_up_delay;
struct thermal_temps {
	int chargerTemp;
	int pp5000Temp;
	int typecStatus;
};

const struct thermal_temps thermal_up[] = { { 53, 46, 0 },
					    { 65, 53, 1 },
					    { 255, 255, 1 } };

const struct thermal_temps thermal_down[] = { { 0, 0, 0 },
					      { 48, 46, 0 },
					      { 54, 53, 1 } };

static void board_thermal_management(void)
{
	int i, charge_port;
	/* battery temp in 0.1 deg C */
	int charger_temp, charger_temp_c;
	int pp500_temp, pp5500_temp_c;

	temp_sensor_read(
		TEMP_SENSOR_ID_BY_DEV(DT_NODELABEL(charger_temp_thermistor)),
		&charger_temp);
	temp_sensor_read(
		TEMP_SENSOR_ID_BY_DEV(DT_NODELABEL(pp5000_z1_temp_thermistor)),
		&pp500_temp);
	charger_temp_c = K_TO_C(charger_temp);
	pp5500_temp_c = K_TO_C(pp500_temp);

	if (extpower_is_present()) {
		charge_port = charge_manager_get_active_charge_port();
		for (i = 0; i < board_get_usb_pd_port_count(); i++) {
			if (i == charge_port) {
				continue;
			}
			if (pd_get_power_role(i) == PD_ROLE_SOURCE) {
				typec_snk_status = 1;
			} else {
				typec_snk_status = 0;
			}
		}
	} else {
		typec_snk_status = 0;
	}

	if (extpower_is_present() && (power_get_state() == POWER_S0)) {
		if (current_level < 2) {
			if ((charger_temp_c >=
				     thermal_up[current_level].chargerTemp &&
			     pp5500_temp_c >=
				     thermal_up[current_level].pp5000Temp)) {
				if (thermal_up[current_level].typecStatus &&
				    typec_snk_status) {
					thermal_up_delay++;
					if (thermal_up_delay >
					    THERMAL_UP_DELAY) {
						thermal_up_delay = 0;
						current_level++;
					}
				} else if (!thermal_up[current_level]
						    .typecStatus) {
					thermal_up_delay++;
					if (thermal_up_delay >
					    THERMAL_UP_DELAY) {
						thermal_up_delay = 0;
						current_level++;
					}
				}
			}
		} else {
			thermal_up_delay = 0;
		}

		if (current_level > 0) {
			if (thermal_down[current_level].typecStatus) {
				if ((charger_temp_c <
				     thermal_down[current_level].chargerTemp) ||
				    (pp5500_temp_c <
				     thermal_down[current_level].pp5000Temp) ||
				    !typec_snk_status) {
					current_level--;
				}
			} else {
				if ((charger_temp_c <
				     thermal_down[current_level].chargerTemp) ||
				    (pp5500_temp_c <
				     thermal_down[current_level].pp5000Temp)) {
					current_level--;
				}
			}
		}
	} else {
		thermal_up_delay = 0;
		current_level = 0;
	}

	if (current_level_pre != current_level) {
		CPRINTS("thermal_control level %d", current_level);
		current_level_pre = current_level;
	}
}
DECLARE_HOOK(HOOK_SECOND, board_thermal_management, HOOK_PRIO_TEMP_SENSOR_DONE);

int charger_profile_override(struct charge_state_data *curr)
{
	int current;

	current = curr->requested_current;

	switch (current_level) {
	case 1:
		curr->requested_current =
			MIN(current, CHARGE_CURRENT_LIMIT_LEVEL1);
		break;
	case 2:
		curr->requested_current =
			MIN(current, CHARGE_CURRENT_LIMIT_LEVEL2);
		break;
	default:
		curr->requested_current = current;
		break;
	}

	return 0;
}

enum ec_status charger_profile_override_get_param(uint32_t param,
						  uint32_t *value)
{
	return EC_RES_INVALID_PARAM;
}

enum ec_status charger_profile_override_set_param(uint32_t param,
						  uint32_t value)
{
	return EC_RES_INVALID_PARAM;
}
