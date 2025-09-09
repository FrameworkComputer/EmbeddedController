/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/ucsi_v3.h"
#include "hooks.h"
#include "temp_sensor/temp_sensor.h"
#include "usbc/pdc_dpm.h"
#include "usbc/pdc_power_mgmt.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(board_thermal, LOG_LEVEL_INF);

struct temp_step {
	int on;
	int off;
	enum usb_typec_current_t ilimi;
};

static const struct temp_step typec_ilim_table[] = {
	{ .on = 0, .off = 0, .ilimi = TC_CURRENT_3_0A },
	{ .on = 53, .off = 46, .ilimi = TC_CURRENT_1_5A },
};

#define NUM_TYPEC_ILIM_LEVELS ARRAY_SIZE(typec_ilim_table)

static void update_typec_ilim(int port)
{
	int rv;
	int chg_temp_c;
	int thermal_sensor0;
	bool level_changed = false;
	static int current_level;
	static int prev_tmp;

	rv = temp_sensor_read(TEMP_SENSOR_ID_BY_DEV(DT_NODELABEL(temp_charger)),
			      &thermal_sensor0);
	chg_temp_c = K_TO_C(thermal_sensor0);
	if (rv != EC_SUCCESS)
		return;

	if (chg_temp_c < prev_tmp &&
	    chg_temp_c <= typec_ilim_table[current_level].off) {
		current_level = current_level - 1;
		/* Prevent level always minus 0 */
		if (current_level < 0)
			current_level = 0;
		else
			level_changed = true;
	} else if (chg_temp_c > prev_tmp &&
		   chg_temp_c >= typec_ilim_table[current_level + 1].on) {
		current_level = current_level + 1;
		/* Prevent level always over table steps */
		if (current_level >= NUM_TYPEC_ILIM_LEVELS)
			current_level = NUM_TYPEC_ILIM_LEVELS - 1;
		else
			level_changed = true;
	}

	prev_tmp = chg_temp_c;

	if (level_changed) {
		enum usb_typec_current_t rp =
			typec_ilim_table[current_level].ilimi;

		LOG_INF("Thermal detected set rp=%d", rp);
		pdc_power_mgmt_set_current_limit(port, rp);
	}
}

static void typec_ilim_control(void)
{
	int i;
	bool any_port_is_source = false;
	int max_port = -1;

	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (pd_get_power_role(i) == PD_ROLE_SOURCE) {
			any_port_is_source = true;
			if (pdc_dpm_get_source_current(i) == 3000) {
				max_port = i;
				break;
			}
		}
	}

	if (!any_port_is_source)
		return;

	if (max_port < 0 || max_port >= board_get_usb_pd_port_count())
		return;

	update_typec_ilim(max_port);
}
DECLARE_HOOK(HOOK_SECOND, typec_ilim_control, HOOK_PRIO_TEMP_SENSOR_DONE);
