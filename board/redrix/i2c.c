/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "compile_time_macros.h"

#include "i2c.h"

/* I2C port map configuration */
const struct i2c_port_t i2c_ports[] = {
	{
		/* I2C0 */
		.name = "sensor",
		.port = I2C_PORT_SENSOR,
		.kbps = 400,
		.scl = GPIO_EC_I2C_SENSOR_SCL,
		.sda = GPIO_EC_I2C_SENSOR_SDA,
	},
	{
		/* I2C1 */
		.name = "tcpc0",
		.port = I2C_PORT_USB_C0_TCPC,
		.kbps = 1000,
		.scl = GPIO_EC_I2C_USB_C0_TCPC_SCL,
		.sda = GPIO_EC_I2C_USB_C0_TCPC_SDA,
	},
	{
		/* I2C2 */
		.name = "ppc0",
		.port = I2C_PORT_USB_C0_PPC,
		.kbps = 1000,
		.scl = GPIO_EC_I2C_USB_C0_PPC_BC_SCL,
		.sda = GPIO_EC_I2C_USB_C0_PPC_BC_SDA,
	},
	{
		/* I2C3 */
		.name = "retimer0",
		.port = I2C_PORT_USB_C0_MUX,
		.kbps = 1000,
		.scl = GPIO_EC_I2C_USB_C0_RT_SCL,
		.sda = GPIO_EC_I2C_USB_C0_RT_SDA,
	},
	{
		/* I2C4 C1 TCPC */
		.name = "tcpc1",
		.port = I2C_PORT_USB_C1_TCPC,
		.kbps = 400,
		.scl = GPIO_EC_I2C_USB_C1_TCPC_SCL,
		.sda = GPIO_EC_I2C_USB_C1_TCPC_SDA,
	},
	{
		/* I2C5 */
		.name = "battery",
		.port = I2C_PORT_BATTERY,
		.kbps = 100,
		.scl = GPIO_EC_I2C_BAT_SCL,
		.sda = GPIO_EC_I2C_BAT_SDA,
	},
	{
		/* I2C6 */
		.name = "ppc1,retimer1",
		.port = I2C_PORT_USB_C1_PPC,
		.kbps = 400,
		.scl = GPIO_EC_I2C_USB_C1_MIX_SCL,
		.sda = GPIO_EC_I2C_USB_C1_MIX_SDA,
	},
	{
		/* I2C7 */
		.name = "eeprom",
		.port = I2C_PORT_EEPROM,
		.kbps = 400,
		.scl = GPIO_EC_I2C_MISC_SCL_R,
		.sda = GPIO_EC_I2C_MISC_SDA_R,
	},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);
