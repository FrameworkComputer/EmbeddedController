/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel ADLRVP-P-DDR4-MEC1521 board-specific configuration */

#include "button.h"
#include "fan.h"
#include "fusb302.h"
#include "gpio.h"
#include "i2c.h"
#include "i2c_bitbang.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "pca9675.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "spi_chip.h"
#include "switch.h"
#include "tablet_mode.h"
#include "timer.h"
#include "uart.h"
#include "usb_pd_tbt.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#include "gpio_list.h" /* Must come after other header files. */

/******************************************************************************/
/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	/*
	 * Port-80 Display, Charger, Battery, IO-expander, EEPROM,
	 * ISH sensor, AUX-rail, power-monitor.
	 */
	[I2C_CHAN_BATT_CHG] = {
		.name = "batt_chg",
		.port = I2C_PORT_CHARGER,
		.kbps = 100,
		.scl = GPIO_SMB_BS_CLK,
		.sda = GPIO_SMB_BS_DATA,
	},
	[I2C_CHAN_TCPC_0] = {
		.name = "typec_0",
		.port = I2C_PORT_TYPEC_0,
		.kbps = 400,
		.scl = GPIO_TYPEC_EC_SMBUS1_CLK_EC,
		.sda = GPIO_TYPEC_EC_SMBUS1_DATA_EC,
	},
	[I2C_CHAN_TCPC_1] = {
		.name = "typec_1",
		.port = I2C_PORT_TYPEC_1,
		.kbps = 400,
		.scl = GPIO_TYPEC_EC_SMBUS3_CLK,
		.sda = GPIO_TYPEC_EC_SMBUS3_DATA,
	},
};
BUILD_ASSERT(ARRAY_SIZE(i2c_ports) == I2C_CHAN_COUNT);
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

const struct i2c_port_t i2c_bitbang_ports[] = {
	[I2C_BITBANG_CHAN_BRD_ID] = {
		.name = "bitbang_brd_id",
		.port = I2C_PORT_CHARGER,
		.kbps = 100,
		.scl = GPIO_SMB_BS_CLK,
		.sda = GPIO_SMB_BS_DATA,
		.drv = &bitbang_drv,
	},
	[I2C_BITBANG_CHAN_IOEX_0] = {
		.name = "bitbang_ioex_0",
		.port = I2C_PORT_TYPEC_0,
		.kbps = 100,
		.scl = GPIO_TYPEC_EC_SMBUS1_CLK_EC,
		.sda = GPIO_TYPEC_EC_SMBUS1_DATA_EC,
		.drv = &bitbang_drv,
	},
};
BUILD_ASSERT(ARRAY_SIZE(i2c_bitbang_ports) == I2C_BITBANG_CHAN_COUNT);
const unsigned int i2c_bitbang_ports_used = ARRAY_SIZE(i2c_bitbang_ports);

/* USB-C TCPC Configuration */
const struct tcpc_config_t tcpc_config[] = {
	[TYPE_C_PORT_0] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TYPEC_0,
			.addr_flags = I2C_ADDR_FUSB302_TCPC_AIC,
		},
		.drv = &fusb302_tcpm_drv,
	},
	[TYPE_C_PORT_1] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TYPEC_1,
			.addr_flags = I2C_ADDR_FUSB302_TCPC_AIC,
		},
		.drv = &fusb302_tcpm_drv,
	},
};
BUILD_ASSERT(ARRAY_SIZE(tcpc_config) == CONFIG_USB_PD_PORT_MAX_COUNT);

/* SPI devices */
const struct spi_device_t spi_devices[] = {
	{ QMSPI0_PORT, 4, GPIO_QMSPI_CS0},
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);
