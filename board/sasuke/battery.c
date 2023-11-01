/* Copyright 2020 The ChromiumOS Authors
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

#define CHARGING_CURRENT_REDUCE 4000
/*
 * Battery info for all sasuke battery types. Note that the fields
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
 *
 * Battery FET Status in Manufacture Access : bit15 & bit14
 *  b'00 - dfet : on / cfet : on
 *  b'01 - dfet : on / cfet : off
 *  b'10 - dfet : off / cfet : off
 *  b'11 - dfet : off / cfet : on
 *  The value b'10 is disconnect_val, so we can use b'01 for cfet_off_val
 */
const struct batt_conf_embed board_battery_info[] = {
	/* SDI Battery Information */
	[BATTERY_SDI] = {
		.manuf_name = "SDI",
		.device_name = "4432D53",
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
					.cfet_off_val = 0x2000,
				},
			},
			.batt_info = {
				.voltage_max            = 8760,
				.voltage_normal         = 7720, /* mV */
				.voltage_min            = 6000, /* mV */
				.precharge_current      = 200,  /* mA */
				.start_charging_min_c   = 0,
				.start_charging_max_c   = 45,
				.charging_min_c         = 0,
				.charging_max_c         = 50,
				.discharging_min_c      = -20,
				.discharging_max_c      = 70,
			},
		},
	},
	/* SWD(Sunwoda) Battery Information */
	[BATTERY_SWD] = {
		.manuf_name = "SWD",
		.device_name = "4432W53",
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
					.cfet_off_val = 0x2000,
				},
			},
			.batt_info = {
				.voltage_max            = 8760,
				.voltage_normal         = 7720, /* mV */
				.voltage_min            = 6000, /* mV */
				.precharge_current      = 200,  /* mA */
				.start_charging_min_c   = 0,
				.start_charging_max_c   = 45,
				.charging_min_c         = 0,
				.charging_max_c         = 50,
				.discharging_min_c      = -20,
				.discharging_max_c      = 70,
			},
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_SDI;

int charger_profile_override(struct charge_state_data *curr)
{
	int current;
	int voltage;

	current = curr->requested_current;
	voltage = curr->requested_voltage;

	voltage -= 100;
	if (current > CHARGING_CURRENT_REDUCE)
		current -= (current / 10);

	curr->requested_voltage = MIN(curr->requested_voltage, voltage);
	curr->requested_current = MIN(curr->requested_current, current);

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
