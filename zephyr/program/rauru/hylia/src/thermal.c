/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "power.h"
#include "temp_sensor/temp_sensor.h"
#include "util.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)
#define CONFIG_BATTERY_ACTIVATION_TIMEOUT (10 * USEC_PER_SEC)

#define DECLINE_LEVEL_COUNT 1
#define INCREMENT_LEVEL_COUNT 3

#define CHARGER_TEMP_ZONE_0_THRESHOLD 70
#define CHARGER_TEMP_ZONE_1_THRESHOLD 75
#define CPU_TEMP_THRESHOLD 55

#define TEMP_ZONE_1_CURRENT_LIMIT 3500 /* mA */

static int current = -1;
static int increment_level, decline_level = 0;
static const struct battery_info *batt_info;
enum power_state chipset_state;

static enum {
	TEMP_ZONE_0, /* not limit */
	TEMP_ZONE_1, /* 3500mA */
} temp_zone = TEMP_ZONE_0;

/* Called by hook task every hook second (1 sec) */
static void update_thermal_state(void)
{
	int cpu_temp, cpu_temp_c;
	int charger_temp, charger_temp_c;

	/*
	 * Keep track of battery temperature range:
	 *
	 *     ZONE_0  ZONE_1
	 * --->------>-------- Temperature (C)
	 *    0      75
	 *     ZONE_0  ZONE_1
	 * ---<------<------- Temperature (C)
	 *    0      70
	 */
	temp_sensor_read(
		TEMP_SENSOR_ID_BY_DEV(DT_NODELABEL(temp_sensor_2_thermistor)),
		&cpu_temp);
	temp_sensor_read(
		TEMP_SENSOR_ID_BY_DEV(DT_NODELABEL(temp_sensor_3_thermistor)),
		&charger_temp);

	cpu_temp_c = K_TO_C(cpu_temp);
	charger_temp_c = K_TO_C(charger_temp);

	if (cpu_temp_c >= CPU_TEMP_THRESHOLD) {
		if (charger_temp_c >= CHARGER_TEMP_ZONE_1_THRESHOLD &&
		    temp_zone != TEMP_ZONE_1) {
			increment_level++;
			decline_level = 0;
		} else if (charger_temp_c < CHARGER_TEMP_ZONE_0_THRESHOLD &&
			   temp_zone != TEMP_ZONE_0) {
			increment_level = 0;
			decline_level++;
		} else {
			increment_level = 0;
			decline_level = 0;
		}
	} else {
		increment_level = 0;
		decline_level++;
	}
	/* If the temperature is increasing, it needs to reach the threshold
	 * three times in a row. If the temperature is increasing, it only
	 * needs to be reached once.
	 */
	if (increment_level == INCREMENT_LEVEL_COUNT) {
		temp_zone = TEMP_ZONE_1;
		increment_level = 0;
	} else if (decline_level == DECLINE_LEVEL_COUNT) {
		temp_zone = TEMP_ZONE_0;
		decline_level = 0;
	}

	switch (temp_zone) {
	case TEMP_ZONE_0:
		/* No current limit */
		current = -1;
		break;
	case TEMP_ZONE_1:
		current = TEMP_ZONE_1_CURRENT_LIMIT;
		break;
	}
}
DECLARE_HOOK(HOOK_SECOND, update_thermal_state, HOOK_PRIO_DEFAULT);

int charger_profile_override(struct charge_state_data *curr)
{
	batt_info = battery_get_info();

	if (get_time().val < CONFIG_BATTERY_ACTIVATION_TIMEOUT &&
	    !gpio_get_level(GPIO_BATT_PRES_ODL) &&
	    curr->batt.voltage <= batt_info->voltage_min) {
		int current = 256;

		curr->requested_current = MAX(curr->requested_current, current);
		curr->requested_voltage =
			MAX(curr->requested_voltage, batt_info->voltage_min);
		curr->state = ST_PRECHARGE;
		curr->batt.flags |= BATT_FLAG_DEEP_CHARGE;

		return -1;
	}

	/*
	 * Precharge must be executed when communication is failed on
	 * dead battery.
	 */
	if (!(curr->batt.flags & BATT_FLAG_RESPONSIVE))
		return -1;

	/* Don't charge if outside of allowable temperature range */
	if (current == 0) {
		curr->batt.flags &= ~BATT_FLAG_WANT_CHARGE;
		if (curr->state != ST_DISCHARGE)
			curr->state = ST_IDLE;
	}

	/* This policy only execute in state s0 */
	chipset_state = power_get_state();

	if (chipset_state != POWER_S0)
		return 0;

	if (current >= 0)
		curr->requested_current = MIN(curr->requested_current, current);

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
