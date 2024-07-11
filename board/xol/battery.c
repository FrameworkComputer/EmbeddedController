/* Copyright 2024 The ChromiumOS Authors
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
/*
 * Battery info for all Xol battery types. Note that the fields
 * start_charging_min/max and charging_min/max are not used for the charger.
 * The effective temperature limits are given by discharging_min/max_c.
 *
 * Fuel Gauge (FG) parameters which are used for determining if the battery
 * is connected, the appropriate ship mode (battery cutoff) command, and the
 * charge/discharge FETs status.
 *
 * Ship mode (battery cutoff) requires 2 writes to the appropriate smart battery
 * register. For some batteries, the charge/discharge FET bits are set when
 * charging/discharging is active, in other types, these bits set mean that
 * charging/discharging is disabled. Therefore, in addition to the mask for
 * these bits, a disconnect value must be specified. Note that for TI fuel
 * gauge, the charge/discharge FET status is found in Operation Status (0x54),
 * but a read of Manufacturer Access (0x00) will return the lower 16 bits of
 * Operation status which contains the FET status bits.
 *
 * The assumption for battery types supported is that the charge/discharge FET
 * status can be read with a sb_read() command and therefore, only the register
 * address, mask, and disconnect value need to be provided.
 */

/* charging data */
#define DEFAULT_DESIGN_CAPACITY 4340
#define CHARGING_VOLTAGE 17624
#define BAT_SERIES 4
#define TC_CHARGING_VOLTAGE 16600
#define CRATE_100 130
#define CFACT_10 9
#define BAT_CELL_VOLT_SPEC 4430
#define BAT_CELL_OVERVOLTAGE (BAT_CELL_VOLT_SPEC - 50)
#define BAT_CELL_MARGIN (BAT_CELL_VOLT_SPEC - 24)
#define BAT_CELL_READY_OVER_VOLT 4150
#define STEP_VOLTAGE_0 16360
#define STEP_VOLTAGE_1 16760

static enum {
	CHARGING_LEVEL_0 = 0,
	CHARGING_LEVEL_1,
	CHARGING_LEVEL_2,
} step_charging_level = CHARGING_LEVEL_0;

struct therm_item {
	int low;
	int high;
};
static enum {
	LOW_TEMP1 = 0,
	LOW_TEMP2,
	LOW_TEMP3,
	NORMAL_TEMP,
	HIGH_TEMP,
	STOP_TEMP,
	TEMP_TYPE_COUNT,
} temp_zone = NORMAL_TEMP;
static const struct therm_item bat_temp_table[] = {
	{ .low = 0, .high = 7 },   { .low = 4, .high = 17 },
	{ .low = 14, .high = 20 }, { .low = 17, .high = 42 },
	{ .low = 39, .high = 51 }, { .low = 45, .high = 500 },
};
BUILD_ASSERT(ARRAY_SIZE(bat_temp_table) == TEMP_TYPE_COUNT);

static struct charge_state_data *charging_data;
static int design_capacity = 0;
static uint16_t bat_cell_volt[BAT_SERIES];
static uint8_t bat_cell_over_volt_flag;
static int bat_cell_ovp_volt;
static uint32_t step1_current = 0;
static uint32_t step2_current = 0;
static uint8_t step_charging_count = 0;

const struct batt_conf_embed board_battery_info[] = {
	/* SDI Battery Information */
	[BATTERY_SDI] = {
		.manuf_name = "SDI",
		.device_name = "4434D43",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x00,
					.reg_mask = 0xc000,
					.disconnect_val = 0x8000,
					.cfet_mask = 0xc000,
					.cfet_off_val = 0x4000,
				},
			},
			.batt_info = {
				.voltage_max            = 17520,
				.voltage_normal	        = 15440, /* mV */
				.voltage_min            = 12000, /* mV */
				.precharge_current      = 200,	/* mA */
				.start_charging_min_c   = 0,
				.start_charging_max_c   = 45,
				.charging_min_c         = 0,
				.charging_max_c         = 55,
				.discharging_min_c      = -20,
				.discharging_max_c      = 70,
			},
		},
	},
	/* SWD(Sunwoda) Battery Information */
	[BATTERY_SWD] = {
		.manuf_name = "SWD",
		.device_name = "4434A43",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x00,
					.reg_mask = 0xc000,
					.disconnect_val = 0x8000,
					.cfet_mask = 0xc000,
					.cfet_off_val = 0x4000,
				},
			},
			.batt_info = {
				.voltage_max            = 17520,
				.voltage_normal         = 15440, /* mV */
				.voltage_min            = 12000, /* mV */
				.precharge_current      = 200,  /* mA */
				.start_charging_min_c   = 0,
				.start_charging_max_c   = 45,
				.charging_min_c         = 0,
				.charging_max_c         = 55,
				.discharging_min_c      = -20,
				.discharging_max_c      = 70,
			},
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_SDI;

enum battery_present battery_hw_present(void)
{
	enum gpio_signal batt_pres;

	batt_pres = GPIO_EC_BATT_PRES_ODL;

	/* The GPIO is low when the battery is physically present */
	return gpio_get_level(batt_pres) ? BP_NO : BP_YES;
}

void find_battery_thermal_zone(int bat_temp)
{
	static int prev_temp;
	int i;

	if (bat_temp < prev_temp) {
		for (i = temp_zone; i > 0; i--) {
			if (bat_temp <= bat_temp_table[i].low)
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

	if (chipset_in_state(CHIPSET_STATE_ON)) {
		/* FCC or DC * 0.45C */
		cal_current *= 9;
		cal_current /= 20;
	} else {
		/* Step1: 0.9C */
		step1_current = cal_current;
		step1_current *= 9;
		step1_current /= 10;
		/* Step2: 0.72C */
		step2_current = step1_current;
		step2_current *= 4;
		step2_current /= 5;

		/* FCC or DC * C-rate * Charge factor */
		cal_current *= (CRATE_100 * CFACT_10);
		cal_current /= 1000;
	}

	*current = (int)cal_current;
}

void set_current_voltage_by_temperature(int *current, int *voltage)
{
	switch (temp_zone) {
	/* low temp 1 */
	case LOW_TEMP1:
		/* DC * 8% */
		*current = design_capacity;
		*current *= 2;
		*current /= 25;
		break;
	/* low temp 2 */
	case LOW_TEMP2:
		/* DC * 24% */
		*current = design_capacity;
		*current *= 6;
		*current /= 25;
		break;
	/* low temp 3 */
	case LOW_TEMP3:
		*current = charging_data->batt.full_capacity;
		if (chipset_in_state(CHIPSET_STATE_ON)) {
			/* FCC * 0.45C */
			*current *= 9;
			*current /= 20;
		} else {
			/* FCC * 0.72C */
			*current *= 18;
			*current /= 25;
		}
		break;
	/* Normal temp */
	case NORMAL_TEMP:
		if (step_charging_level == CHARGING_LEVEL_1)
			*current = (int)step1_current;
		else if (step_charging_level == CHARGING_LEVEL_2)
			*current = (int)step2_current;
		break;
	/* high temp */
	case HIGH_TEMP:
		if (check_ready_for_high_temperature()) {
			/* DC * 30% */
			*current = design_capacity;
			*current *= 3;
			*current /= 10;
			*voltage = TC_CHARGING_VOLTAGE;
		} else {
			temp_zone = NORMAL_TEMP;
			if (step_charging_level == CHARGING_LEVEL_1)
				*current = (int)step1_current;
			else if (step_charging_level == CHARGING_LEVEL_2)
				*current = (int)step2_current;
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
		int bat_temp = DECI_KELVIN_TO_CELSIUS(curr->batt.temperature);
		find_battery_thermal_zone(bat_temp);

		/* charge stop */
		if (temp_zone == STOP_TEMP) {
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
static void reduce_input_voltage_when_full(void)
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

static void check_step_charging(void)
{
	int32_t charger_mw;

	charger_mw = charge_manager_get_power_limit_uw() / 100000;

	/*
	 *  1. if charging in suspend
	 *  2. Not sub-power
	 *  3. Normal temperature
	 */
	if (chipset_in_state(CHIPSET_STATE_ON) || (charger_mw < 300) ||
	    (charging_data->state != ST_CHARGE) || (temp_zone != NORMAL_TEMP)) {
		step_charging_level = CHARGING_LEVEL_0;
		step_charging_count = 0;
		return;
	}

	if (step_charging_level == CHARGING_LEVEL_2)
		return;

	if ((step_charging_level == CHARGING_LEVEL_0) &&
	    (charging_data->batt.voltage > STEP_VOLTAGE_0)) {
		if (step_charging_count < 5) {
			step_charging_count++;
		} else {
			step_charging_count = 0;
			step_charging_level = CHARGING_LEVEL_1;
		}
	} else if ((step_charging_level == CHARGING_LEVEL_1) &&
		   (charging_data->batt.voltage > STEP_VOLTAGE_1)) {
		if (step_charging_count < 5) {
			step_charging_count++;
		} else {
			step_charging_count = 0;
			step_charging_level = CHARGING_LEVEL_2;
		}
	} else {
		step_charging_count = 0;
	}
}
DECLARE_HOOK(HOOK_SECOND, check_step_charging, HOOK_PRIO_DEFAULT);
