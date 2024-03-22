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
#include "driver/charger/isl9241.h"
#include "gpio.h"
#include "hooks.h"
/*
 * Battery info for all Osiris battery types. Note that the fields
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
	/* COSMX AP22ABN Battery Information */
	[BATTERY_COSMX_AP22ABN] = {
		.manuf_name = "COSMX KT0030B003",
		.device_name = "AP22ABN",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x3A,
					.reg_data = { 0xC574, 0xC574 },
				},
				.fet = {
					.reg_addr = 0x0,
					.reg_mask = 0x8000,
					.disconnect_val = 0x0000,
					.cfet_mask = 0x4000,
					.cfet_off_val = 0x4000,
				},
			},
			.batt_info = {
				.voltage_max            = 13440,
				.voltage_normal         = 11670,
				.voltage_min            = 9000,
				.precharge_current      = 567,
				.start_charging_min_c   = 0,
				.start_charging_max_c   = 50,
				.charging_min_c         = 0,
				.charging_max_c         = 60,
				.discharging_min_c      = -20,
				.discharging_max_c      = 75,
			},
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_COSMX_AP22ABN;

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

/* Set the DCPROCHOT base on battery over discharging current about 7A */
static void set_dc_prochot(void)
{
	/*
	 * Only bits 13:8 are usable for this register, any other bits will be
	 * truncated.  Valid values are 256 mA to 16128 mA at 256 mA intervals.
	 */

	isl9241_set_dc_prochot(CHARGER_SOLO, 0x1B00);
}
DECLARE_HOOK(HOOK_INIT, set_dc_prochot, HOOK_PRIO_DEFAULT);
