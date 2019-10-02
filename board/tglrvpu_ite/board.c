/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel TGL-U-RVP-ITE board-specific configuration */

#include "button.h"
#include "extpower.h"
#include "i2c.h"
#include "intc.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "switch.h"
#include "system.h"
#include "tablet_mode.h"
#include "uart.h"

#include "gpio_list.h"

#define CPRINTS(format, args...) cprints(CC_COMMAND, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_COMMAND, format, ## args)

/* TCPC gpios */
const struct tcpc_gpio_config_t tcpc_gpios[] = {
	[TYPE_C_PORT_0] = {
		.vbus = {
			.pin = GPIO_USB_C0_VBUS_INT,
			.pin_pol = 1,
		},
		.src = {
			.pin = GPIO_USB_C0_SRC_EN,
			.pin_pol = 1,
		},
		.snk = {
			.pin = GPIO_USB_C0_SNK_EN_L,
			.pin_pol = 0,
		},
		.vconn = {
			.cc1_pin = GPIO_USB_C0_CC1_VCONN_EN,
			.cc2_pin = GPIO_USB_C0_CC2_VCONN_EN,
			.pin_pol = 1,
		},
		.src_ilim = {
			.pin = GPIO_USB_C0_SRC_HI_ILIM,
			.pin_pol = 1,
		},
	},
	[TYPE_C_PORT_1] = {
		.vbus = {
			.pin = GPIO_USB_C1_VBUS_INT,
			.pin_pol = 1,
		},
		.src = {
			.pin = GPIO_USB_C1_SRC_EN,
			.pin_pol = 1,
		},
		.snk = {
			.pin = GPIO_USB_C1_SNK_EN_L,
			.pin_pol = 0,
		},
		.vconn = {
			.cc1_pin = GPIO_USB_C1_CC1_VCONN_EN,
			.cc2_pin = GPIO_USB_C1_CC2_VCONN_EN,
			.pin_pol = 1,
		},
		.src_ilim = {
			.pin = GPIO_USB_C1_SRC_HI_ILIM,
			.pin_pol = 1,
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(tcpc_gpios) == CONFIG_USB_PD_PORT_MAX_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	/* Flash EC */
	[I2C_CHAN_FLASH] = {
		.name = "chan-A",
		.port = IT83XX_I2C_CH_A,
		.kbps = 100,
		.scl = GPIO_I2C_A_SCL,
		.sda = GPIO_I2C_A_SDA,
	},
	/*
	 * Port-80 Display, Charger, Battery, IO-expanders, EEPROM,
	 * IMVP9, AUX-rail, power-monitor.
	 */
	[I2C_CHAN_BATT_CHG] = {
		.name = "batt_chg",
		.port = IT83XX_I2C_CH_B,
		.kbps = 100,
		.scl = GPIO_I2C_B_SCL,
		.sda = GPIO_I2C_B_SDA,
	},
	/* Retimers, PDs */
	[I2C_CHAN_RETIMER] = {
		.name = "retimer",
		.port = IT83XX_I2C_CH_E,
		.kbps = 100,
		.scl = GPIO_I2C_E_SCL,
		.sda = GPIO_I2C_E_SDA,
	},
};
BUILD_ASSERT(ARRAY_SIZE(i2c_ports) == I2C_CHAN_COUNT);
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/*
 * Returns board information (board id[7:0] and Fab id[15:8]) on success
 * -1 on error.
 */
int board_get_version(void)
{
	int port0, port1;
	int fab_id, board_id, bom_id;

	if (ioexpander_read_intelrvp_version(&port0, &port1))
		return -1;
	/*
	 * Port0: bit 0   - BOM ID(2)
	 *        bit 2:1 - FAB ID(1:0) + 1
	 * Port1: bit 7:6 - BOM ID(1:0)
	 *        bit 5:0 - BOARD ID(5:0)
	 */
	bom_id = ((port1 & 0xC0) >> 6) | ((port0 & 0x01) << 2);
	fab_id = ((port0 & 0x06) >> 1) + 1;
	board_id = port1 & 0x3F;

	CPRINTS("BID:0x%x, FID:0x%x, BOM:0x%x", board_id, fab_id, bom_id);

	return board_id | (fab_id << 8);
}
