/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_state.h"
#include "charger.h"
#include "console.h"
#include "extpower.h"
#include "temp_sensor/temp_sensor.h"
#include "util.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(smart_battery);

/*
 * Dynamic changing charge current.
 */

struct temp_chg_step {
	int low; /* temp thershold ('C) to lower level*/
	int high; /* temp thershold ('C) to higher level */
	int current; /* charging limitation (mA) */
};

static const struct temp_chg_step temp_chg_table[] = {
	{ .low = 0, .high = 56, .current = __INT32_MAX__ },
	{ .low = 50, .high = 100, .current = 2000 },
};
#define NUM_TEMP_CHG_LEVELS ARRAY_SIZE(temp_chg_table)

#undef BOARD_TEMP_TEST

#ifdef BOARD_TEMP_TEST
static int manual_temp = -1;
#endif

__override int board_charger_profile_override(struct charge_state_data *curr)
{
	static int current_level;
	int charger_temp, charger_temp_c;

	if (curr->state != ST_CHARGE)
		return 0;

	temp_sensor_read(TEMP_SENSOR_ID_BY_DEV(DT_NODELABEL(temp_charger)),
			 &charger_temp);

	charger_temp_c = K_TO_C(charger_temp);

#ifdef BOARD_TEMP_TEST
	if (manual_temp != -1)
		charger_temp_c = manual_temp;
	LOG_WRN("chg_temp_c: %d", charger_temp_c);
#endif

	if (charger_temp_c <= temp_chg_table[current_level].low)
		current_level--;
	else if (charger_temp_c >= temp_chg_table[current_level].high)
		current_level++;

	if (current_level < 0)
		current_level = 0;

	if (current_level >= NUM_TEMP_CHG_LEVELS)
		current_level = NUM_TEMP_CHG_LEVELS - 1;

	curr->requested_current = MIN(curr->requested_current,
				      temp_chg_table[current_level].current);

#ifdef BOARD_TEMP_TEST
	LOG_WRN("level: %d, batt_current: %d, limit_current: %d", current_level,
		curr->requested_current, temp_chg_table[current_level].current);
#endif

	return EC_SUCCESS;
}

#ifdef BOARD_TEMP_TEST
static int command_temp_test(int argc, const char **argv)
{
	char *e;
	int t;

	if (argc > 1) {
		t = strtoi(argv[1], &e, 0);
		if (*e) {
			LOG_WRN("Invalid test temp");
			return EC_ERROR_INVAL;
		}
		manual_temp = t;
		LOG_WRN("manual temp is %d", manual_temp);
		return EC_SUCCESS;
	}

	manual_temp = -1;
	LOG_WRN("manual temp reset");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(tt, command_temp_test, "[temperature]",
			"set manual temperature for test");
#endif
