/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Trogdor baseboard-specific configuration */

#include "charge_state.h"
#include "charger.h"
#include "driver/charger/isl923x.h"
#include "i2c.h"
#include "power.h"
#include "usb_pd.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

/* Wake-up pins for hibernate */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L,
	GPIO_EC_RST_ODL,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

void board_hibernate_late(void)
{
	/* Set the hibernate GPIO to turn off the rails */
	gpio_set_level(GPIO_HIBERNATE_L, 0);
}

/* Power signal list. Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	[SC7180_AP_RST_ASSERTED] = {
		GPIO_AP_RST_L,
		POWER_SIGNAL_ACTIVE_LOW | POWER_SIGNAL_DISABLE_AT_BOOT,
		"AP_RST_ASSERTED"},
	[SC7180_PS_HOLD] = {
		GPIO_PS_HOLD,
		POWER_SIGNAL_ACTIVE_HIGH,
		"PS_HOLD"},
	[SC7180_POWER_GOOD] = {
		GPIO_POWER_GOOD,
		POWER_SIGNAL_ACTIVE_HIGH,
		"POWER_GOOD"},
	[SC7180_WARM_RESET] = {
		GPIO_WARM_RESET_L,
		POWER_SIGNAL_ACTIVE_HIGH,
		"WARM_RESET_L"},
	[SC7180_AP_SUSPEND] = {
		GPIO_AP_SUSPEND,
		POWER_SIGNAL_ACTIVE_HIGH,
		"AP_SUSPEND"},
	[SC7180_DEPRECATED_AP_RST_REQ] = {
		GPIO_DEPRECATED_AP_RST_REQ,
		POWER_SIGNAL_ACTIVE_HIGH,
		"DEPRECATED_AP_RST_REQ"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};

int board_allow_i2c_passthru(int port)
{
	return (port == I2C_PORT_VIRTUAL_BATTERY);
}

int charger_profile_override(struct charge_state_data *curr)
{
	int usb_mv;
	int port;

	if (curr->state != ST_CHARGE)
		return 0;

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
