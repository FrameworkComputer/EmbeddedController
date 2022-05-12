/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "compile_time_macros.h"

#include "i2c.h"

/* I2C port map configuration */
const struct i2c_port_t i2c_ports[] = {
	{
		/* I2C2 C0 PPC*/
		.name = "ppc C0",
		.port = I2C_PORT_USB_C0_PPC,
		.kbps = 400,
		.scl = GPIO_EC_I2C_USB_C0_C2_PPC_SCL,
		.sda = GPIO_EC_I2C_USB_C0_C2_PPC_SDA,
	},
	{
		/* I2C4 C0 TCPC */
		.name = "tcpc C0",
		.port = I2C_PORT_USB_C0_TCPC,
		.kbps = 400,
		.scl = GPIO_EC_I2C_USB_C0_TCPC_SCL,
		.sda = GPIO_EC_I2C_USB_C0_TCPC_SDA,
	},
	{
		/* I2C6 C0 BC1.2 */
		.name = "bc12 C0",
		.port = I2C_PORT_USB_C0_BC12,
		.kbps = 1000,
		.scl = GPIO_EC_I2C_USB_C0_MIX_SCL,
		.sda = GPIO_EC_I2C_USB_C0_MIX_SDA,
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
