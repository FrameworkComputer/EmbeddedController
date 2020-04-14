/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Volteer board-specific configuration */

#include "button.h"
#include "common.h"
#include "accelgyro.h"
#include "driver/accel_bma2x2.h"
#include "driver/als_tcs3400.h"
#include "driver/sync.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "tablet_mode.h"
#include "uart.h"
#include "usb_pd_tbt.h"
#include "util.h"

#include "gpio_list.h" /* Must come after other header files. */

static void board_init(void)
{
	/* TODO */
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

__override enum tbt_compat_cable_speed board_get_max_tbt_speed(int port)
{
	/* Routing length exceeds 205mm prior to connection to re-timer */
	if (port == USBC_PORT_C1)
		return TBT_SS_U32_GEN1_GEN2;

	/*
	 * Thunderbolt-compatible mode not supported
	 *
	 * TODO (b/153995632): All the USB-C ports need to support same speed.
	 * Need to fix once USB-C feature set is known for Halvor.
	 */
	return TBT_SS_RES_0;
}

__override bool board_is_tbt_usb4_port(int port)
{
	/*
	 * On the volteer reference board 1 only port 1 supports TBT & USB4
	 *
	 * TODO (b/153995632): All the USB-C ports need to support same
	 * features. Need to fix once USB-C feature set is known for Halvor.
	 */
	return port == USBC_PORT_C1;
}

/******************************************************************************/
/* I2C port map configuration */
const struct i2c_port_t i2c_ports[] = {
	{
		.name = "sensor",
		.port = I2C_PORT_SENSOR,
		.kbps = 400,
		.scl = GPIO_EC_I2C_0_SCL,
		.sda = GPIO_EC_I2C_0_SDA,
	},
	{
		.name = "usb_c0",
		.port = I2C_PORT_USB_C0,
		.kbps = 1000,
		.scl = GPIO_EC_I2C_1_SCL,
		.sda = GPIO_EC_I2C_1_SDA,
	},
	{
		.name = "usb_c1",
		.port = I2C_PORT_USB_C1,
		.kbps = 1000,
		.scl = GPIO_EC_I2C_2_SCL,
		.sda = GPIO_EC_I2C_2_SDA,
	},
	{
		.name = "usb_bb_retimer",
		.port = I2C_PORT_USB_BB_RETIMER,
		.kbps = 100,
		.scl = GPIO_EC_I2C_3_SCL,
		.sda = GPIO_EC_I2C_3_SDA,
	},
	{
		.name = "usb_c2",
		.port = I2C_PORT_USB_C2,
		.kbps = 1000,
		.scl = GPIO_EC_I2C_4_SCL,
		.sda = GPIO_EC_I2C_4_SDA,
	},
	{
		.name = "power",
		.port = I2C_PORT_POWER,
		.kbps = 100,
		.scl = GPIO_EC_I2C_5_SCL,
		.sda = GPIO_EC_I2C_5_SDA,
	},
	{
		.name = "eeprom",
		.port = I2C_PORT_EEPROM,
		.kbps = 400,
		.scl = GPIO_EC_I2C_7_SCL,
		.sda = GPIO_EC_I2C_7_SDA,
	},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);
