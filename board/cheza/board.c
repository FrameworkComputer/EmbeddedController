/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Cheza board-specific configuration */

#include "adc_chip.h"
#include "button.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "lid_switch.h"
#include "pi3usb9281.h"
#include "power.h"
#include "power_button.h"
#include "switch.h"

#include "gpio_list.h"

/* 8-bit I2C address */
#define PI3USB9281_I2C_ADDR	0x4a
#define DA9313_I2C_ADDR		0xd0
#define CHARGER_I2C_ADDR	0x12

/* Wake-up pins for hibernate */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

const struct adc_t adc_channels[] = {};

/* Power signal list. Must match order of enum power_signal. */
/*
 * PMIC pulls up the AP_RST_L signal to power-on AP. Once AP is up, it then
 * pulls up the PS_HOLD signal.
 *
 *      +--> GPIO_AP_RST_L >--+
 *   PMIC                     AP
 *      +--< GPIO_PS_HOLD <---+
 *
 * When AP initiates shutdown, it pulls down the PS_HOLD signal to notify
 * PMIC. PMIC then pulls down the AP_RST_L.
 *
 * TODO(b/78455067): By far, we use the AP_RST_L signal to indicate AP in a
 * good state. Address the issue of AP-initiated warm reset.
 */
const struct power_signal_info power_signal_list[] = {
	{GPIO_AP_RST_L, POWER_SIGNAL_ACTIVE_HIGH, "POWER_GOOD"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* I2C port map */
const struct i2c_port_t i2c_ports[] = {
	{"power",   I2C_PORT_POWER,  400, GPIO_I2C0_SCL,  GPIO_I2C0_SDA},
	{"tcpc0",   I2C_PORT_TCPC0,  400, GPIO_I2C1_SCL,  GPIO_I2C1_SDA},
	{"tcpc1",   I2C_PORT_TCPC1,  400, GPIO_I2C2_SCL,  GPIO_I2C2_SDA},
	{"eeprom",  I2C_PORT_EEPROM, 400, GPIO_I2C5_SCL,  GPIO_I2C5_SDA},
	{"sensor",  I2C_PORT_SENSOR, 400, GPIO_I2C7_SCL,  GPIO_I2C7_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* Initialize board. */
static void board_init(void)
{
	/*
	 * Hack to enable the internal switch in BC1.2
	 * TODO(waihong): Enable BC1.2 autodetection.
	 */
	i2c_write8(I2C_PORT_TCPC0, PI3USB9281_I2C_ADDR, PI3USB9281_REG_CONTROL,
		   0x1b);  /* Manual switch on port-0 */
	i2c_write8(I2C_PORT_TCPC0, PI3USB9281_I2C_ADDR, PI3USB9281_REG_MANUAL,
		   0x24);  /* Connection of D+ and D- */
	i2c_write8(I2C_PORT_TCPC1, PI3USB9281_I2C_ADDR, PI3USB9281_REG_CONTROL,
		   0x1b);  /* Manual switch on port-1 */
	i2c_write8(I2C_PORT_TCPC1, PI3USB9281_I2C_ADDR, PI3USB9281_REG_MANUAL,
		   0x24);  /* Connection of D+ and D- */

	/*
	 * Disable SwitchCap auto-boot and make EN pin level-trigger
	 * TODO(b/77957956): Remove it after hardware fix.
	 */
	i2c_write8(I2C_PORT_POWER, DA9313_I2C_ADDR, 0x02, 0x34);

	/*
	 * Increase AdapterCurrentLimit{1,2} to max (6080mA)
	 */
	i2c_write16(I2C_PORT_POWER, CHARGER_I2C_ADDR, 0x3B, 0x17c0);
	i2c_write16(I2C_PORT_POWER, CHARGER_I2C_ADDR, 0x3F, 0x17c0);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
