/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Trogdor family-specific USB-C configuration */

#include "charger.h"
#include "charger/isl923x_public.h"
#include "charge_state.h"
#include "console.h"
#include "temp_sensor.h"
#include "usb_pd.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};

struct temp_chg_step {
	int low;	/* temp thershold ('C) to lower level*/
	int high;	/* temp thershold ('C) to higher level */
	int current;	/* charging limitation (mA) */
};

static const struct temp_chg_step temp_chg_table[] = {
	{.low =  0, .high = 50, .current = 3000},	/* Lv0: normal charge */
	{.low = 48, .high = 53, .current = 1500},
	{.low = 51, .high = 56, .current = 1000},
	{.low = 54, .high = 100, .current = 800},
};
#define NUM_TEMP_CHG_LEVELS ARRAY_SIZE(temp_chg_table)

int charger_profile_override(struct charge_state_data *curr)
{
	static int current_level;
	int charger_temp, charger_temp_c;
	int usb_mv;
	int port;

	if (curr->state != ST_CHARGE)
		return 0;

	/* charge current control depends on temp if the system is on */
	if (chipset_in_state(CHIPSET_STATE_ON)) {
		temp_sensor_read(TEMP_SENSOR_SYS2, &charger_temp);
		charger_temp_c = K_TO_C(charger_temp);

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
	}

	/* Lower the max requested voltage to 5V when battery is full. */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF) &&
	    !(curr->batt.flags & BATT_FLAG_BAD_STATUS) &&
	    !(curr->batt.flags & BATT_FLAG_WANT_CHARGE) &&
	    (curr->batt.status & STATUS_FULLY_CHARGED))
		usb_mv = 5000;
	else
		usb_mv = PD_MAX_VOLTAGE_MV;

	if (pd_get_max_voltage() != usb_mv) {
		CPRINTS("VBUS limited to %dmV", usb_mv);
		for (port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; port++)
			pd_set_external_voltage_limit(port, usb_mv);
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
