/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "compile_time_macros.h"

#include "i2c.h"

/* I2C port map configuration */
const struct i2c_port_t i2c_ports[] = {
	{
		/* I2C1 */
		.name = "tcpc0,1",
		.port = I2C_PORT_USB_C0_C1_TCPC,
		.kbps = 1000,
		.scl = GPIO_EC_I2C_USB_C0_C1_TCPC_SCL,
		.sda = GPIO_EC_I2C_USB_C0_C1_TCPC_SDA,
	},
	{
		/* I2C2 */
		.name = "ppc0,1",
		.port = I2C_PORT_USB_C0_C1_PPC,
		.kbps = 1000,
		.scl = GPIO_EC_I2C_USB_C0_C1_PPC_BC_SCL,
		.sda = GPIO_EC_I2C_USB_C0_C1_PPC_BC_SDA,
	},
	{
		/* I2C3 */
		.name = "retimer0,1",
		.port = I2C_PORT_USB_C0_C1_MUX,
		.kbps = 1000,
		.scl = GPIO_EC_I2C_USB_C0_C1_RT_SCL,
		.sda = GPIO_EC_I2C_USB_C0_C1_RT_SDA,
	},
	{
		/* I2C5 */
		.name = "wireless_charger",
		.port = I2C_PORT_QI,
		.kbps = 400,
		.scl = GPIO_EC_I2C_QI_SCL,
		.sda = GPIO_EC_I2C_QI_SDA,
	},
	{
		/* I2C6 */
		.name = "usba_mix0,1",
		.port = I2C_PORT_USB_A0_A1_MIX,
		.kbps = 100,
		.scl = GPIO_EC_I2C_USB_A0_A1_MIX_SCL,
		.sda = GPIO_EC_I2C_USB_A0_A1_MIX_SDA,
	},
	{
		/* I2C7 */
		.name = "eeprom",
		.port = I2C_PORT_EEPROM,
		.kbps = 400,
		.scl = GPIO_EC_I2C_MISC_SCL,
		.sda = GPIO_EC_I2C_MISC_SDA,
	},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);
