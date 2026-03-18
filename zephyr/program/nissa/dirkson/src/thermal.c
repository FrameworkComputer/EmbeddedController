/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board.h"
#include "driver/tcpm/tcpm.h"
#include "hooks.h"
#include "temp_sensor/temp_sensor.h"
#include "usb_pd.h"
#include "usbc_ppc.h"

#include <zephyr/logging/log.h>

#include <ap_power/ap_power.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

struct typec_ilim_step {
	int on;
	int off;
	enum tcpc_rp_value typec_rp;
};

static const struct typec_ilim_step typec_ilim_table[] = {
	{ .on = 0, .off = 0, .typec_rp = TYPEC_RP_3A0 },
	{ .on = 84, .off = 76, .typec_rp = TYPEC_RP_1A5 },
	{ .on = 90, .off = 82, .typec_rp = TYPEC_RP_USB },
};

#define NUM_TYPEC_ILIM_LEVELS ARRAY_SIZE(typec_ilim_table)

static void typec_ilim_control(void)
{
	int rv;
	int chg_temp_c;
	int thermal_sensor0;
	bool level_changed = false;
	static int current_level;
	static int prev_tmp;

	/* Leave if the ppc is not sourcing power. */
	if (!ppc_is_sourcing_vbus(0))
		return;

	rv = temp_sensor_read(TEMP_SENSOR_ID_BY_DEV(DT_NODELABEL(temp_ambient)),
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
		enum tcpc_rp_value rp =
			typec_ilim_table[current_level].typec_rp;

		LOG_INF("Temp changed to %dC: Rp=%d", chg_temp_c, rp);
		ppc_set_vbus_source_current_limit(0, rp);
		tcpm_select_rp_value(0, rp);
		pd_update_contract(0);
	}
}
DECLARE_HOOK(HOOK_SECOND, typec_ilim_control, HOOK_PRIO_TEMP_SENSOR_DONE);
