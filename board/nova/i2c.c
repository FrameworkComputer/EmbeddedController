/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "compile_time_macros.h"
#include "i2c.h"

/* I2C port map configuration */
const struct i2c_port_t i2c_ports[] = {
	{
		/* I2C0 C1 TCPC */
		.name = "tcpc1",
		.port = I2C_PORT_USB_C1_TCPC,
		.kbps = 400,
		.scl = GPIO_EC_I2C_USB_C1_TCPC_SCL,
		.sda = GPIO_EC_I2C_USB_C1_TCPC_SDA,
	},
	{
		/* I2C1 */
		.name = "tcpc0,2",
		.port = I2C_PORT_USB_C0_C2_TCPC,
		.kbps = 1000,
		.scl = GPIO_EC_I2C_USB_C0_C2_TCPC_SCL,
		.sda = GPIO_EC_I2C_USB_C0_C2_TCPC_SDA,
	},
	{
		/* I2C2 */
		.name = "ppc0,2",
		.port = I2C_PORT_USB_C0_C2_PPC,
		.kbps = 1000,
		.scl = GPIO_EC_I2C_USB_C0_C2_PPC_SCL,
		.sda = GPIO_EC_I2C_USB_C0_C2_PPC_SDA,
	},
	{
		/* I2C3 */
		.name = "retimer0,2",
		.port = I2C_PORT_USB_C0_C2_MUX,
		.kbps = 1000,
		.scl = GPIO_EC_I2C_USB_C0_C2_RT_SCL,
		.sda = GPIO_EC_I2C_USB_C0_C2_RT_SDA,
	},
	{
		/* I2C4 */
		.name = "ppc1",
		.port = I2C_PORT_USB_C1_PPC,
		.kbps = 400,
		.scl = GPIO_EC_I2C_USB_C1_PPC_SCL,
		.sda = GPIO_EC_I2C_USB_C1_PPC_SDA,
	},
	{
		/* I2C5 */
		.name = "pse",
		.port = I2C_PORT_PSE,
		.kbps = 400,
		.scl = GPIO_EC_I2C_LTC_SCL,
		.sda = GPIO_EC_I2C_LTC_SDA,
	},
	{
		/* I2C6 */
		.name = "ADB",
		.port = I2C_PORT_ADB,
		.kbps = 1000,
		.scl = GPIO_EC_I2C_ADB_SCL,
		.sda = GPIO_EC_I2C_ADB_SDA,
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
