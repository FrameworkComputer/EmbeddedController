/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Trogdor baseboard-specific configuration */

#include "charger.h"
#include "driver/charger/isl923x.h"
#include "i2c.h"
#include "power.h"

/* Wake-up pins for hibernate */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L,
	GPIO_EC_RST_ODL,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

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
	[SC7180_PMIC_FAULT_L] = {
		GPIO_PMIC_FAULT_L,
		POWER_SIGNAL_ACTIVE_HIGH | POWER_SIGNAL_DISABLE_AT_BOOT,
		"PMIC_FAULT_L"},
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

/* I2C port map */
const struct i2c_port_t i2c_ports[] = {
	{"power",   I2C_PORT_POWER,  100, GPIO_EC_I2C_POWER_SCL,
					  GPIO_EC_I2C_POWER_SDA},
	{"tcpc0",   I2C_PORT_TCPC0, 1000, GPIO_EC_I2C_USB_C0_PD_SCL,
					  GPIO_EC_I2C_USB_C0_PD_SDA},
	{"tcpc1",   I2C_PORT_TCPC1, 1000, GPIO_EC_I2C_USB_C1_PD_SCL,
					  GPIO_EC_I2C_USB_C1_PD_SDA},
	{"eeprom",  I2C_PORT_EEPROM, 400, GPIO_EC_I2C_EEPROM_SCL,
					  GPIO_EC_I2C_EEPROM_SDA},
	{"sensor",  I2C_PORT_SENSOR, 400, GPIO_EC_I2C_SENSOR_SCL,
					  GPIO_EC_I2C_SENSOR_SDA},
};

const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};

const unsigned int chg_cnt = ARRAY_SIZE(chg_chips);
