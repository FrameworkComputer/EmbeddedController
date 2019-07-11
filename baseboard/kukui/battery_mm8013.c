/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "charge_state.h"
#include "charger_mt6370.h"
#include "console.h"
#include "driver/tcpm/mt6370.h"
#include "ec_commands.h"
#include "util.h"

#define TEMP_OUT_OF_RANGE TEMP_ZONE_COUNT

#define BATT_ID 0

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

enum battery_type {
	BATTERY_SCUD = 0,
	BATTERY_COUNT
};

static const struct battery_info info[] = {
	[BATTERY_SCUD] = {
		.voltage_max		= 4400,
		.voltage_normal		= 3850,
		.voltage_min		= 3400,
		.precharge_current	= 256,
		.start_charging_min_c	= 0,
		.start_charging_max_c	= 45,
		.charging_min_c		= 0,
		.charging_max_c		= 50,
		.discharging_min_c	= -20,
		.discharging_max_c	= 60,
	},
};

const struct battery_info *battery_get_info(void)
{
	return &info[BATT_ID];
}

enum battery_disconnect_state battery_get_disconnect_state(void)
{
	if (battery_is_present() == BP_YES)
		return BATTERY_NOT_DISCONNECTED;
	return BATTERY_DISCONNECTED;
}

int charger_profile_override(struct charge_state_data *curr)
{
	/* TODO(b:138269954): Add battery configs for krane. */

#ifdef VARIANT_KUKUI_CHARGER_MT6370
	mt6370_charger_profile_override(curr);
#endif /* CONFIG_CHARGER_MT6370 */

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
