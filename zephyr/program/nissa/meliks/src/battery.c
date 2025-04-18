/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery_fuel_gauge.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "math_util.h"
#include "usb_pd.h"
#include "util.h"

/* charging data */
#define DEFAULT_DESIGN_CAPACITY 5723
#define CHARGING_VOLTAGE 8600
#define BAT_SERIES 2
#define TC_CHARGING_VOLTAGE 8300
#define CRATE_100 80
#define CFACT_10 9
#define BAT_CELL_VOLT_SPEC 4400
#define BAT_CELL_OVERVOLTAGE (BAT_CELL_VOLT_SPEC - 50)
#define BAT_CELL_MARGIN (BAT_CELL_VOLT_SPEC - 32)
#define BAT_CELL_READY_OVER_VOLT 4150

struct therm_item {
	int low;
	int high;
};
static enum {
	STOP_LOW_TEMP = 0,
	LOW_TEMP3,
	LOW_TEMP2,
	LOW_TEMP1,
	NORMAL_TEMP,
	HIGH_TEMP,
	STOP_HIGH_TEMP,
	TEMP_TYPE_COUNT,
} temp_zone = NORMAL_TEMP;
static const struct therm_item bat_temp_table[] = {
	{ .low = -100, .high = 2 }, { .low = 0, .high = 10 },
	{ .low = 8, .high = 17 },   { .low = 15, .high = 20 },
	{ .low = 18, .high = 42 },  { .low = 40, .high = 51 },
	{ .low = 46, .high = 500 },
};
BUILD_ASSERT(ARRAY_SIZE(bat_temp_table) == TEMP_TYPE_COUNT);

static struct charge_state_data *charging_data;
static int design_capacity = 0;
static uint16_t bat_cell_volt[BAT_SERIES];
static uint8_t bat_cell_over_volt_flag;
static int bat_cell_ovp_volt;

void find_battery_thermal_zone(int bat_temp)
{
	static int prev_temp;
	int i;

	if (bat_temp < prev_temp) {
		for (i = temp_zone; i > 0; i--) {
			if (bat_temp < bat_temp_table[i].low)
				temp_zone = i - 1;
			else
				break;
		}
	} else if (bat_temp > prev_temp) {
		for (i = temp_zone; i < ARRAY_SIZE(bat_temp_table); i++) {
			if (bat_temp >= bat_temp_table[i].high)
				temp_zone = i + 1;
			else
				break;
		}
	}

	if (temp_zone < 0)
		temp_zone = 0;

	if (temp_zone >= ARRAY_SIZE(bat_temp_table))
		temp_zone = ARRAY_SIZE(bat_temp_table) - 1;

	prev_temp = bat_temp;
}

void check_battery_cell_voltage(void)
{
	int rv;
	static int cell_check_flag = 0;
	static uint8_t idx = 0;
	int data;
	uint16_t max_voltage, min_voltage, delta_voltage;
	static uint8_t over_volt_count[BAT_SERIES] = {
		0,
	};

	if (charging_data->state == ST_CHARGE) {
		cell_check_flag = 1;
		rv = sb_read(SB_OPTIONAL_MFG_FUNC1 + idx, &data);
		if (rv)
			return;
		bat_cell_volt[idx] = data;

		if (bat_cell_volt[idx] >= BAT_CELL_OVERVOLTAGE &&
		    bat_cell_over_volt_flag == 0) {
			over_volt_count[idx]++;
			if (over_volt_count[idx] >= 4) {
				max_voltage = min_voltage = bat_cell_volt[idx];
				for (int i = 0; i < BAT_SERIES; i++) {
					if (bat_cell_volt[i] > max_voltage)
						max_voltage = bat_cell_volt[i];
					if (bat_cell_volt[i] < min_voltage &&
					    bat_cell_volt[i] != 0)
						min_voltage = bat_cell_volt[i];
				}
				delta_voltage = max_voltage - min_voltage;
				if ((delta_voltage < 600) &&
				    (delta_voltage > 10)) {
					bat_cell_over_volt_flag = 1;
					bat_cell_ovp_volt =
						BAT_CELL_MARGIN * BAT_SERIES -
						delta_voltage *
							(BAT_SERIES - 1);
				}
			}
		} else {
			over_volt_count[idx] = 0;
		}

		idx++;
		if (idx >= BAT_SERIES)
			idx = 0;
	} else {
		if (cell_check_flag != 0) {
			cell_check_flag = 0;
			for (int i = 0; i < BAT_SERIES; i++) {
				over_volt_count[i] = 0;
			}
			bat_cell_over_volt_flag = 0;
			bat_cell_ovp_volt = 0;
		}
	}
}
DECLARE_HOOK(HOOK_TICK, check_battery_cell_voltage, HOOK_PRIO_DEFAULT);

int check_ready_for_high_temperature(void)
{
	for (int i = 0; i < BAT_SERIES; i++) {
		if (bat_cell_volt[i] >= BAT_CELL_READY_OVER_VOLT) {
			return 0;
		}
	}

	return 1;
}

void set_current_volatage_by_capacity(int *current, int *voltage)
{
	int rateFCDC = 0;
	uint32_t cal_current = 0;

	*current = 0;
	*voltage = CHARGING_VOLTAGE;

	cal_current = charging_data->batt.full_capacity * 100;
	cal_current += (design_capacity / 2);
	cal_current /= design_capacity;
	rateFCDC = (int)cal_current;

	/* calculate current & voltage */
	if (rateFCDC <= 85) {
		cal_current = charging_data->batt.full_capacity;

		/* ChargingVoltage - (170mV * series) */
		*voltage -= (170 * BAT_SERIES);
	} else if (rateFCDC <= 99) {
		cal_current = charging_data->batt.full_capacity;

		/* ChargingVoltage - ((1-FCC/DC)*100*series) -
		 * (25*series) */
		*voltage -= (((100 - rateFCDC) * 10 * BAT_SERIES) +
			     (25 * BAT_SERIES));
	} else {
		cal_current = design_capacity;
	}

	/* FCC or DC * 0.45C */
	cal_current *= 9;
	cal_current /= 20;

	*current = (int)cal_current;
}

void set_current_voltage_by_temperature(int *current, int *voltage)
{
	switch (temp_zone) {
	/* low temp step 3 */
	case LOW_TEMP3:
		/* DC * 8% */
		*current = design_capacity;
		*current *= 2;
		*current /= 25;
		break;
	/* low temp step 2 */
	case LOW_TEMP2:
		/* DC * 24% */
		*current = design_capacity;
		*current *= 6;
		*current /= 25;
		break;
	/* low temp step 1 */
	case LOW_TEMP1:
		*current = charging_data->batt.full_capacity;
		/* FCC * 0.45C */
		*current *= 9;
		*current /= 20;
		break;
	/* high temp */
	case HIGH_TEMP:
		if (check_ready_for_high_temperature()) {
			/* DC * 0.45C */
			*current = design_capacity;
			*current *= 9;
			*current /= 20;
			*voltage = TC_CHARGING_VOLTAGE;
		} else {
			temp_zone = NORMAL_TEMP;
		}
		break;
	default:
		break;
	}
}

int charger_profile_override(struct charge_state_data *curr)
{
	int data_c;
	int data_v;

	enum charge_state state;

	charging_data = curr;

	if (curr->batt.is_present == BP_YES) {
		/*
		 * Precharge must be executed when communication is failed on
		 * dead battery.
		 */
		if (!(curr->batt.flags & BATT_FLAG_RESPONSIVE))
			return 0;

		if (!(curr->batt.flags & BATT_FLAG_BAD_TEMPERATURE)) {
			int bat_temp =
				DECI_KELVIN_TO_CELSIUS(curr->batt.temperature);
			find_battery_thermal_zone(bat_temp);
		}

		/* charge stop */
		if (temp_zone == STOP_LOW_TEMP || temp_zone == STOP_HIGH_TEMP) {
			curr->requested_current = curr->requested_voltage = 0;
			curr->batt.flags &= ~BATT_FLAG_WANT_CHARGE;
			curr->state = ST_IDLE;

			return 0;
		}

		state = curr->state;
		if (state == ST_CHARGE) {
			if (design_capacity == 0) {
				if (battery_design_capacity(&design_capacity)) {
					design_capacity =
						DEFAULT_DESIGN_CAPACITY;
				}
			}
			set_current_volatage_by_capacity(&data_c, &data_v);
			set_current_voltage_by_temperature(&data_c, &data_v);

			if (bat_cell_over_volt_flag) {
				if (data_v > bat_cell_ovp_volt)
					data_v = bat_cell_ovp_volt;
			}

			if (curr->requested_current != data_c &&
			    /* If charging current of battery is 0(fully
			     * charged), then EC shouldn't change it for AC
			     * standby power */
			    curr->requested_current != 0) {
				curr->requested_current = data_c;
			}
			curr->requested_voltage = data_v;
		} else {
			temp_zone = NORMAL_TEMP;
		}
	} else {
		design_capacity = 0;
		temp_zone = NORMAL_TEMP;
	}

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
