/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Charger profile override for fast charging
 */

#include "charger_profile_override.h"
#include "console.h"
#include "ec_commands.h"
#include "util.h"

#ifdef CONFIG_CMD_CHARGER_PROFILE_OVERRIDE_TEST
static int fast_charge_test_on;
static int test_flag_temp;
static int test_flag_vtg;
static int test_temp_c;
static int test_vtg_mV = -1;
#endif

static int fast_charging_allowed = 1;

int charger_profile_override_common(struct charge_state_data *curr,
			const struct fast_charge_params *fast_chg_params,
			const struct fast_charge_profile **prev_chg_prof_info,
			int batt_vtg_max)
{
	int i, voltage_range;
	/* temp in 0.1 deg C */
	int temp_c = curr->batt.temperature - 2731;
	int temp_ranges = fast_chg_params->total_temp_ranges;
	const struct fast_charge_profile *chg_profile_info =
				fast_chg_params->chg_profile_info;

#ifdef CONFIG_CMD_CHARGER_PROFILE_OVERRIDE_TEST
	if (fast_charge_test_on && test_vtg_mV != -1) {
		temp_c = TEMPC_TENTHS_OF_DEG(test_temp_c);
		curr->batt.voltage = test_vtg_mV;

		if (test_flag_temp)
			curr->batt.flags |= BATT_FLAG_BAD_TEMPERATURE;
		else
			curr->batt.flags &= BATT_FLAG_BAD_TEMPERATURE;

		if (test_flag_vtg)
			curr->batt.flags |= BATT_FLAG_BAD_VOLTAGE;
		else
			curr->batt.flags &= BATT_FLAG_BAD_VOLTAGE;
	}
#endif

	/*
	 * Determine temperature range.
	 * If temp reading was bad, use last range.
	 */
	if (!(curr->batt.flags & BATT_FLAG_BAD_TEMPERATURE)) {
		while (chg_profile_info && temp_ranges) {
			if (temp_c <= chg_profile_info->temp_c) {
				*prev_chg_prof_info = chg_profile_info;
				break;
			}
			chg_profile_info++;
			temp_ranges--;
		}

		/* Invalid charge profile selected */
		if (!chg_profile_info || !temp_ranges)
			return -1;
	}

	/*
	 * If the battery voltage reading is bad or the battery voltage is
	 * greater than or equal to the lower limit or the battery voltage is
	 * not in the charger profile voltage range, consider battery has high
	 * voltage range so that we charge at lower current limit.
	 */
	voltage_range = CONFIG_CHARGER_PROFILE_VOLTAGE_RANGES - 1;

	if (!(curr->batt.flags & BATT_FLAG_BAD_VOLTAGE)) {
		for (i = 0; i < CONFIG_CHARGER_PROFILE_VOLTAGE_RANGES - 1;
			i++) {
			if (curr->batt.voltage <
					fast_chg_params->voltage_mV[i]) {
				voltage_range = i;
				break;
			}
		}
	}

	/*
	 * If we are not charging or we aren't using fast charging profiles,
	 * then do not override desired current and voltage.
	 */
	if (curr->state != ST_CHARGE || !fast_charging_allowed)
		return 0;

	/*
	 * Okay, impose our custom will:
	 */
	curr->requested_current =
			(*prev_chg_prof_info)->current_mA[voltage_range];
	curr->requested_voltage = curr->requested_current ? batt_vtg_max : 0;

#ifdef CONFIG_CMD_CHARGER_PROFILE_OVERRIDE_TEST
	if (fast_charge_test_on)
		ccprintf("Fast charge profile i=%dmA, v=%dmV\n",
			curr->requested_current, curr->requested_voltage);
#endif

	return 0;
}

/* Customs options controllable by host command. */
#define PARAM_FASTCHARGE (CS_PARAM_CUSTOM_PROFILE_MIN + 0)

enum ec_status charger_profile_override_get_param(uint32_t param,
						  uint32_t *value)
{
	if (param == PARAM_FASTCHARGE) {
		*value = fast_charging_allowed;
		return EC_RES_SUCCESS;
	}
	return EC_RES_INVALID_PARAM;
}

enum ec_status charger_profile_override_set_param(uint32_t param,
						  uint32_t value)
{
	if (param == PARAM_FASTCHARGE) {
		fast_charging_allowed = value;
		return EC_RES_SUCCESS;
	}
	return EC_RES_INVALID_PARAM;
}

#ifdef CONFIG_CMD_CHARGER_PROFILE_OVERRIDE
static int command_fastcharge(int argc, char **argv)
{
	if (argc > 1 && !parse_bool(argv[1], &fast_charging_allowed))
		return EC_ERROR_PARAM1;

	ccprintf("fastcharge %s\n", fast_charging_allowed ? "on" : "off");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(fastcharge, command_fastcharge,
			"[on|off]",
			"Get or set fast charging profile");
#endif

/*
 * Manipulate the temperature and voltage values and check if the correct
 * fast charging profile is selected.
 */
#ifdef CONFIG_CMD_CHARGER_PROFILE_OVERRIDE_TEST
static int command_fastcharge_test(int argc, char **argv)
{
	char *e;
	int test_on;

	if (argc > 1 && !parse_bool(argv[1], &test_on))
		return EC_ERROR_PARAM2;

	/* Check if only tuurn printf message on / off */
	if (argc == 2) {
		fast_charge_test_on = test_on;
		test_vtg_mV = -1;

		return EC_SUCCESS;
	}

	/* Validate the input parameters */
	if ((test_on && argc != 6) || !test_on)
		return EC_ERROR_PARAM_COUNT;

	test_flag_temp = strtoi(argv[2], &e, 0);
	if (*e || test_flag_temp > 1 || test_flag_temp < 0)
		return EC_ERROR_PARAM3;

	test_flag_vtg = strtoi(argv[3], &e, 0);
	if (*e || test_flag_vtg > 1 || test_flag_vtg < 0)
		return EC_ERROR_PARAM4;

	test_temp_c = strtoi(argv[4], &e, 0);
	if (*e)
		return EC_ERROR_PARAM5;

	test_vtg_mV = strtoi(argv[5], &e, 0);
	if (*e || test_vtg_mV < 0) {
		test_vtg_mV = -1;
		return EC_ERROR_PARAM6;
	}

	fast_charge_test_on = 1;

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(fastchgtest, command_fastcharge_test,
			"off | on tempflag[1|0] vtgflag[1|0] temp_c vtg_mV",
			"Check if fastcharge profile works");
#endif
