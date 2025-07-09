/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "console.h"
#include "extpower.h"
#include "hooks.h"
#include "power.h"
#include "temp_sensor/temp_sensor.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

static int thermals[6];
static int thermal_cyc;

int charger_profile_override(struct charge_state_data *curr)
{
	int power_temp, power_temp_c;
	int charger_temp, charger_temp_c, charger_temp_ave;
	int cpu_temp, cpu_temp_c;
	int current;
	int charger_temp_sum = 0;
	enum power_state chipset_state = power_get_state();

	/*
	 * Keep track of battery temperature range:
	 *
	 *     ZONE_0  ZONE_1   ZONE_2  ZONE_3
	 * --->------>-------->-------->------>--- Temperature (C)
	 *    0      50       55       60     80
	 *     ZONE_0  ZONE_1   ZONE_2  ZONE_3
	 * ---<------<--------<--------<------<--- Temperature (C)
	 *    0      45        50       55     80
	 */
	enum {
		TEMP_ZONE_0, /* not limit */
		TEMP_ZONE_1, /* 2500mA */
		TEMP_ZONE_2, /* 1800mA */
		TEMP_ZONE_3, /* 1000mA */
		TEMP_ZONE_COUNT,
		TEMP_OUT_OF_RANGE = TEMP_ZONE_COUNT /* Not charging */
	} temp_zone;

	if (!(curr->batt.flags & BATT_FLAG_RESPONSIVE))
		return 0;

	current = curr->requested_current;

	temp_sensor_read(
		TEMP_SENSOR_ID_BY_DEV(DT_NODELABEL(temp_sensor_pwr_thermistor)),
		&power_temp);

	temp_sensor_read(
		TEMP_SENSOR_ID_BY_DEV(DT_NODELABEL(temp_sensor_cpu_thermistor)),
		&cpu_temp);

	temp_sensor_read(
		TEMP_SENSOR_ID_BY_DEV(DT_NODELABEL(temp_sensor_chg_thermistor)),
		&charger_temp);

	charger_temp_c = K_TO_C(charger_temp);
	cpu_temp_c = K_TO_C(cpu_temp);
	power_temp_c = K_TO_C(power_temp);

	if ((charger_temp_c >= 125) || (charger_temp_c <= -30))
		return 0;

	/*
	 * thermals[5] is the average of the previous 5 calculations.
	 * charger_temp_ave is the calculated average of the previous
	 * 4 times plus this time.
	 */
	thermals[thermal_cyc] = charger_temp_c;
	thermal_cyc = (thermal_cyc + 1) % 5;
	for (int i = 0; i < 5; i++)
		charger_temp_sum += thermals[i];

	charger_temp_ave = (charger_temp_sum + 2.5) / 5;
	if (chipset_state != POWER_S0) {
		if ((curr->batt.flags & BATT_FLAG_BAD_TEMPERATURE) ||
		    (charger_temp_ave > 79))
			temp_zone = TEMP_OUT_OF_RANGE;
		else
			temp_zone = TEMP_ZONE_0;
	} else {
		if (cpu_temp_c >= 43) {
			if (thermals[5] <= charger_temp_ave) {
				if ((curr->batt.flags &
				     BATT_FLAG_BAD_TEMPERATURE) ||
				    (charger_temp_ave > 79))
					temp_zone = TEMP_OUT_OF_RANGE;
				else if (charger_temp_ave >= 60)
					temp_zone = TEMP_ZONE_3;
				else if (charger_temp_ave >= 55)
					temp_zone = TEMP_ZONE_2;
				else if (charger_temp_ave >= 50)
					temp_zone = TEMP_ZONE_1;
				else
					temp_zone = TEMP_ZONE_0;
			} else {
				if ((curr->batt.flags &
				     BATT_FLAG_BAD_TEMPERATURE) ||
				    (charger_temp_ave > 79))
					temp_zone = TEMP_OUT_OF_RANGE;
				else if (charger_temp_ave < 45)
					temp_zone = TEMP_ZONE_0;
				else if (charger_temp_ave < 50)
					temp_zone = TEMP_ZONE_1;
				else if (charger_temp_ave < 55)
					temp_zone = TEMP_ZONE_2;
				else
					temp_zone = TEMP_ZONE_3;
			}
		} else {
			if ((curr->batt.flags & BATT_FLAG_BAD_TEMPERATURE) ||
			    (charger_temp_ave > 79))
				temp_zone = TEMP_OUT_OF_RANGE;
			else
				temp_zone = TEMP_ZONE_0;
		}
	}
	thermals[5] = charger_temp_ave;

	switch (temp_zone) {
	case TEMP_ZONE_0:
		/* Don't change anything in zone0*/
		break;
	case TEMP_ZONE_1:
		current = 2500;
		break;
	case TEMP_ZONE_2:
		current = 1800;
		break;
	case TEMP_ZONE_3:
		current = 1000;
		break;
	case TEMP_OUT_OF_RANGE:
		/* Don't charge if outside of allowable temperature range */
		current = 0;
		curr->batt.flags &= ~BATT_FLAG_WANT_CHARGE;
		if (curr->state != ST_DISCHARGE)
			curr->state = ST_IDLE;
		break;
	}

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
