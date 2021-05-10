/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery_fuel_gauge.h"
#include "charge_state.h"
#include "common.h"

/*
 * Battery info for all sasukette battery types. Note that the fields
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
const struct board_batt_params board_battery_info[] = {
	/* SDI Battery Information */
	[BATTERY_SDI] = {
		.fuel_gauge = {
			.manuf_name = "SDI",
			.device_name = "4402D51",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.mfgacc_support = 0,
				.reg_addr = 0x00,
				.reg_mask = 0xc000,
				.disconnect_val = 0x8000,
				.cfet_mask = 0xc000,
				.cfet_off_val = 0x2000,
			}
		},
		.batt_info = {
			.voltage_max		= 8800,
			.voltage_normal		= 7700, /* mV */
			.voltage_min		= 6000, /* mV */
			.precharge_current	= 200,	/* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 45,
			.charging_min_c		= 0,
			.charging_max_c		= 50,
			.discharging_min_c	= -20,
			.discharging_max_c	= 70,
		},
	}
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_SDI;

static int swelling_flag = -1;
static int prev_ac = -1;
static int charger_flag = -1;

enum swelling_data {
	SWELLING_TRIGGER_5 = 1,
	SWELLING_TRIGGER_15,
	SWELLING_TRIGGER_45,
	SWELLING_TRIGGER_50,
	SWELLING_RECOVERY_10,
	SWELLING_RECOVERY_20,
	SWELLING_RECOVERY_50,
	SWELLING_DATA_COUNT
};

int charger_profile_override(struct charge_state_data *curr)
{
	static timestamp_t chargeCnt;
	int bat_temp_c = (curr->batt.temperature - 2731) / 10;

	/*
	 *	start charge temp control
	 *
	 *	if bat_temp >= 45 or bat_temp <= 0 when adapter plugging in,
	 *	stop charge
	 *	if 0 < bat_temp < 45 when adapter plugging in, charge normal
	 */
	if (curr->ac != prev_ac) {
		if (curr->ac) {
			if ((bat_temp_c <= 0) || (bat_temp_c >= 45))
				charger_flag = 1;
		}
		prev_ac = curr->ac;
	}

	if (charger_flag) {
		curr->requested_current = 0;
		curr->requested_voltage = 0;
		curr->batt.flags &= ~BATT_FLAG_WANT_CHARGE;
		curr->state = ST_IDLE;
	}

	if ((bat_temp_c > 0) && (bat_temp_c < 45))
		charger_flag = 0;

/*
 *	battery swelling control
 *
 *	trigger condition		|	recovery condition
 *	1. bat_temp < 5 &&		|	1.batt_temp >= 10
 *	bat_cell_voltage < 4.15		|
 *					|	cv = (cell CV-50mv)*series
 *	cv = 4150mv*series = 8300mv	|	   = 8700mv
 *	cc = FCC*C_rate*0.4		|	cc = FCC*C_rate*0.4 = 1464ma
 *					|
 *	2. bat_temp < 15 &&		|	2.batt_temp >= 20,
 *	bat_cell_voltage < 4.15		|
 *					|	cv = (cell CV-50mv)*series
 *	cv = 4150mv*series = 8300mv	|	   = 8700mv
 *	cc = FCC*C_rate*0.4= 1464ma	|	cc = FCC*C_rate*0.9 = 3294ma
 *					|
 *	3. bat_temp >= 45 &&		|	3. batt_temp < 43
 *	bat_cell_voltage < 4.15		|
 *					|	cv = (cell CV-50mv)*series
 *	cv = 4150mv*series = 8300mv	|	   = 8700mv
 *	cc = FCC*C_rate*0.45= 1647ma	|	cc = FCC*C_rate*0.9 = 3294ma
 *					|
 *	4. bat_temp >= 50		|	4.batt_temp < 45,
 *	stop charge			|	recovery charge
 */
	if (curr->ac && !charger_flag) {

		/*
		 * battery swelling trigger condition
		 */
		if (curr->batt.voltage < 8300) {
			if (bat_temp_c < 5)
				swelling_flag = SWELLING_TRIGGER_5;
			else if (bat_temp_c < 15)
				swelling_flag = SWELLING_TRIGGER_15;

			if (bat_temp_c >= 50)
				swelling_flag = SWELLING_TRIGGER_50;
			else if (bat_temp_c >= 45) {
				if (!(swelling_flag & SWELLING_TRIGGER_50))
					swelling_flag = SWELLING_TRIGGER_45;
			}
		}

		/*
		 * battery swelling recovery condition
		 */
		if (swelling_flag) {
			if ((bat_temp_c >= 10) && (bat_temp_c < 20))
				swelling_flag = SWELLING_RECOVERY_10;
			else if ((bat_temp_c >= 20) && (bat_temp_c < 43))
				swelling_flag = SWELLING_RECOVERY_20;
			else if ((bat_temp_c >= 43) && (bat_temp_c < 45))
				swelling_flag = SWELLING_RECOVERY_50;
		}

		switch (swelling_flag) {
		case SWELLING_TRIGGER_5:
			curr->requested_voltage = 4150 * 2;
			curr->requested_current = 5230 * 0.7 * 0.4;

			if ((curr->batt.current < 300)) {
				if (chargeCnt.val == 0) {
					chargeCnt.val = get_time().val
							+ 30 * SECOND;
				} else if (timestamp_expired(chargeCnt, NULL)) {
					curr->requested_current = 0;
					curr->requested_voltage = 0;
					curr->batt.flags &=
							~BATT_FLAG_WANT_CHARGE;
					curr->state = ST_IDLE;
				}
			} else {
				chargeCnt.val = 0;
			}
			break;

		case SWELLING_TRIGGER_15:
			curr->requested_current = 5230 * 0.7 * 0.4;
			chargeCnt.val = 0;
			break;

		case SWELLING_TRIGGER_45:
			curr->requested_voltage = 4150 * 2;
			curr->requested_current = 5230 * 0.7 * 0.45;

			if ((curr->batt.current < 300)) {
				if (chargeCnt.val == 0) {
					chargeCnt.val = get_time().val
							+ 30 * SECOND;
				} else if (timestamp_expired(chargeCnt, NULL)) {
					curr->requested_current = 0;
					curr->requested_voltage = 0;
					curr->batt.flags &=
							~BATT_FLAG_WANT_CHARGE;
					curr->state = ST_IDLE;
				}
			} else {
				chargeCnt.val = 0;
			}
			break;

		case SWELLING_TRIGGER_50:
			curr->requested_current = 0;
			curr->requested_voltage = 0;
			curr->batt.flags &= ~BATT_FLAG_WANT_CHARGE;
			curr->state = ST_IDLE;
			chargeCnt.val = 0;
			break;

		case SWELLING_RECOVERY_10:
			curr->requested_current = 5230 * 0.7 * 0.4;
			chargeCnt.val = 0;
			break;

		case SWELLING_RECOVERY_20:
		case SWELLING_RECOVERY_50:
			curr->requested_current = 5230 * 0.7 * 0.9;
			chargeCnt.val = 0;
			break;

		default:
			curr->requested_voltage += 100;
			break;
		}
	} else {
		swelling_flag = 0;
		chargeCnt.val = 0;
	}

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
