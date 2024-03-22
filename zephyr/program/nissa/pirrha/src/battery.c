/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery_fuel_gauge.h"
#include "charge_state.h"
#include "common.h"
#include "hooks.h"
#include "usb_pd.h"
#include "util.h"

/* charging current is limited to 0.45C */
#define CHARGING_CURRENT_45C 2601

int charger_profile_override(struct charge_state_data *curr)
{
	if ((chipset_in_state(CHIPSET_STATE_ON)) &&
	    (curr->requested_current > CHARGING_CURRENT_45C))
		curr->requested_current = CHARGING_CURRENT_45C;

	return 0;
}

/* Customs options controllable by host command. */
#define PARAM_FASTCHARGE (CS_PARAM_CUSTOM_PROFILE_MIN + 0)

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

/* Lower our input voltage to 5V in S0iX when battery is full. */
#define PD_VOLTAGE_WHEN_FULL 5000
test_export_static void reduce_input_voltage_when_full(void)
{
	static int saved_input_voltage = -1;
	int max_pd_voltage_mv = pd_get_max_voltage();
	int port;

	if (charge_get_percent() == 100 &&
	    chipset_in_state(CHIPSET_STATE_ANY_SUSPEND)) {
		if (max_pd_voltage_mv != PD_VOLTAGE_WHEN_FULL) {
			saved_input_voltage = max_pd_voltage_mv;
			max_pd_voltage_mv = PD_VOLTAGE_WHEN_FULL;
		}
	} else if (saved_input_voltage != -1) {
		if (max_pd_voltage_mv == PD_VOLTAGE_WHEN_FULL)
			max_pd_voltage_mv = saved_input_voltage;
		saved_input_voltage = -1;
	}

	if (pd_get_max_voltage() != max_pd_voltage_mv) {
		for (port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; port++)
			pd_set_external_voltage_limit(port, max_pd_voltage_mv);
	}
}
DECLARE_HOOK(HOOK_SECOND, reduce_input_voltage_when_full, HOOK_PRIO_DEFAULT);
