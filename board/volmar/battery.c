/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "cbi.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "charger_profile_override.h"
#include "common.h"
#include "compile_time_macros.h"
#include "gpio.h"
/*
 * Battery info for all Volmar battery types. Note that the fields
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
const struct batt_conf_embed board_battery_info[] = {
	/* COSMX AP20CBL Battery Information */
	[BATTERY_COSMX_AP20CBL] = {
		.manuf_name = "COSMX KT0030B002",
		.device_name = "AP20CBL",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x3A,
					.reg_data = { 0xC574, 0xC574 },
				},
				.fet = {
					.reg_addr = 0x0,
					.reg_mask = 0x2000,
					.disconnect_val = 0x2000,
					.cfet_mask = 0x4000,
					.cfet_off_val = 0x4000,
				},
				.flags = FUEL_GAUGE_FLAG_MFGACC,
			},
			.batt_info = {
				.voltage_max            = 13200,
				.voltage_normal         = 11550,
				.voltage_min            = 9000,
				.precharge_current      = 256,
				.start_charging_min_c   = 0,
				.start_charging_max_c   = 50,
				.charging_min_c         = 0,
				.charging_max_c         = 60,
				.discharging_min_c      = -20,
				.discharging_max_c      = 75,
			},
		},
	},
	/* COSMX AP20CBL Battery Information (new firmware ver) */
	[BATTERY_COSMX_AP20CBL_004] = {
		.manuf_name = "COSMX KT0030B004",
		.device_name = "AP20CBL",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x3A,
					.reg_data = { 0xC574, 0xC574 },
				},
				.fet = {
					.reg_addr = 0x0,
					.reg_mask = 0x2000,
					.disconnect_val = 0x2000,
					.cfet_mask = 0x4000,
					.cfet_off_val = 0x4000,
				},
				.flags = FUEL_GAUGE_FLAG_MFGACC,
			},
			.batt_info = {
				.voltage_max            = 13200,
				.voltage_normal         = 11550,
				.voltage_min            = 9000,
				.precharge_current      = 256,
				.start_charging_min_c   = 0,
				.start_charging_max_c   = 50,
				.charging_min_c         = 0,
				.charging_max_c         = 60,
				.discharging_min_c      = -20,
				.discharging_max_c      = 75,
			},
		},
	},
	/* LGC AP18C8K Battery Information */
	[BATTERY_LGC_AP18C8K] = {
		.manuf_name = "LGC KT0030G020",
		.device_name = "AP18C8K",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x3A,
					.reg_data = { 0xC574, 0xC574 },
				},
				.fet = {
					.reg_addr = 0x43,
					.reg_mask = 0x0001,
					.disconnect_val = 0x0,
				},
			},
			.batt_info = {
				.voltage_max            = 13050,
				.voltage_normal         = 11250,
				.voltage_min            = 9000,
				.precharge_current      = 256,
				.start_charging_min_c   = 0,
				.start_charging_max_c   = 50,
				.charging_min_c         = 0,
				.charging_max_c         = 60,
				.discharging_min_c      = -20,
				.discharging_max_c      = 75,
			},
		},
	},
	/* LGC AP19B8M Battery Information */
	[BATTERY_AP19B8M] = {
		.manuf_name = "LGC KT0030G024",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x3A,
					.reg_data = { 0xC574, 0xC574 },
				},
				.fet = {
					.reg_addr = 0x43,
					.reg_mask = 0x0001,
					.disconnect_val = 0x0,
					.cfet_mask = 0x0002,
					.cfet_off_val = 0x0000,
				},
			},
			.batt_info = {
				.voltage_max          = 13350,
				.voltage_normal       = 11610,
				.voltage_min          = 9000,
				.precharge_current    = 256,
				.start_charging_min_c = 0,
				.start_charging_max_c = 50,
				.charging_min_c       = 0,
				.charging_max_c       = 60,
				.discharging_min_c    = -20,
				.discharging_max_c    = 75,
			},
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_COSMX_AP20CBL;

enum battery_present battery_hw_present(void)
{
	/* The GPIO is low when the battery is physically present */
	return gpio_get_level(GPIO_EC_BATT_PRES_ODL) ? BP_NO : BP_YES;
}

static int charger_should_discharge_on_ac(struct charge_state_data *curr)
{
	/* can not discharge on AC without battery */
	if (curr->batt.is_present != BP_YES)
		return 0;

	/* Do not discharge when battery disconnect */
	if (battery_get_disconnect_state() != BATTERY_NOT_DISCONNECTED)
		return 0;

	/* Do not discharge on AC if the battery is still waking up */
	if ((curr->batt.flags & BATT_FLAG_BAD_STATUS) ||
	    (!(curr->batt.flags & BATT_FLAG_WANT_CHARGE) &&
	     !(curr->batt.status & STATUS_FULLY_CHARGED)))
		return 0;

	/*
	 * In heavy load (>3A being withdrawn from VSYS) the DCDC of the
	 * charger operates on hybrid mode. This causes a slight voltage
	 * ripple on VSYS that falls in the audible noise frequency (single
	 * digit kHz range). This small ripple generates audible noise in
	 * the output ceramic capacitors (caps on VSYS and any input of
	 * DCDC under VSYS).
	 *
	 * To overcome this issue, force battery discharging when battery
	 * full, So the battery MOS of NVDC charger will turn on always,
	 * it make the Vsys same as Vbat and the noise has been improved.
	 */
	if (!battery_is_cut_off() &&
	    !(curr->batt.flags & BATT_FLAG_WANT_CHARGE) &&
	    (curr->batt.status & STATUS_FULLY_CHARGED))
		return 1;

	return 0;
}

/*
 * This can override the smart battery's charging profile. To make a change,
 * modify one or more of requested_voltage, requested_current, or state.
 * Leave everything else unchanged.
 *
 * Return the next poll period in usec, or zero to use the default (which is
 * state dependent).
 */
int charger_profile_override(struct charge_state_data *curr)
{
	int disch_on_ac = charger_should_discharge_on_ac(curr);

	charger_discharge_on_ac(disch_on_ac);

	if (disch_on_ac) {
		curr->state = ST_DISCHARGE;
		return 0;
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
