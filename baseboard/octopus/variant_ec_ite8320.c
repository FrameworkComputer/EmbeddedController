/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common code for VARIANT_OCTOPUS_EC_ITE8320 configuration */

#include "gpio.h"
#include "i2c.h"
#include "util.h"

/******************************************************************************/
/* Wake up pins */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_POWER_BUTTON_L,
	/*
	 * The PPC interrupts (which fire when Vbus changes) is a proxy for
	 * AC_PRESENT. This allows us to turn off the PPC SNK FETS during
	 * hibernation which saves power. Once the EC wakes up, it will enable
	 * the SNK FETs and power will make it to the rest of the system.
	 */
	GPIO_USB_C0_PD_INT_ODL,
	GPIO_USB_C1_PD_INT_ODL,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/******************************************************************************/
/* I2C port map configuration */
const struct i2c_port_t i2c_ports[] = {
	{ .name = "power",
	  .port = IT83XX_I2C_CH_A,
	  .kbps = 100,
	  .scl = GPIO_I2C0_SCL,
	  .sda = GPIO_I2C0_SDA },
	{ .name = "sensor",
	  .port = IT83XX_I2C_CH_B,
	  .kbps = 100,
	  .scl = GPIO_I2C1_SCL,
	  .sda = GPIO_I2C1_SDA },
	{ .name = "usbc0",
	  .port = IT83XX_I2C_CH_C,
	  .kbps = 400,
	  .scl = GPIO_I2C2_SCL,
	  .sda = GPIO_I2C2_SDA },
	{ .name = "usbc1",
	  .port = IT83XX_I2C_CH_E,
	  .kbps = 400,
	  .scl = GPIO_I2C4_SCL,
	  .sda = GPIO_I2C4_SDA },
	{ .name = "eeprom",
	  .port = IT83XX_I2C_CH_F,
	  .kbps = 100,
	  .scl = GPIO_I2C5_SCL,
	  .sda = GPIO_I2C5_SDA },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);
