/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel Jasperlake RVP with ITE EC board specific configuration */

#include "button.h"
#include "charger.h"
#include "driver/charger/isl923x.h"
#include "extpower.h"
#include "i2c.h"
#include "icelake.h"
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

/* Charger Chips */
const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};

/******************************************************************************/
/* PWROK signal configuration */
/*
 * On JSLRVP the ALL_SYS_PWRGD, VCCST_PWRGD, PCH_PWROK, and SYS_PWROK
 * signals are handled by the board. No EC control needed.
 */
const struct intel_x86_pwrok_signal pwrok_signal_assert_list[] = {};
const int pwrok_signal_assert_count = ARRAY_SIZE(pwrok_signal_assert_list);

const struct intel_x86_pwrok_signal pwrok_signal_deassert_list[] = {};
const int pwrok_signal_deassert_count = ARRAY_SIZE(pwrok_signal_assert_list);

/*
 * Returns board information (board id[7:0] and Fab id[15:8]) on success
 * -1 on error.
 */
int board_get_version(void)
{
	int fab_id;
	int board_id;
	int bom_id;

	if (ioexpander_read_intelrvp_version(&fab_id, &board_id))
		return -1;
	/*
	 * Port0: bit 1:0 - FAB ID(1:0) + 1
	 * Port1: bit 7:5 - BOM ID(2:0)
	 *        bit 4:0 - BOARD ID(4:0)
	 */
	fab_id = (fab_id & 0x03) + 1;
	bom_id = ((board_id & 0xE0) >> 5);
	board_id &= 0x1F;

	CPRINTS("BID:0x%x, FID:0x%x, BOM:0x%x", board_id, fab_id, bom_id);

	return board_id | (fab_id << 8);
}
