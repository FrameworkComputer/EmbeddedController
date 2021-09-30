/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel ADLRVP-ITE board-specific configuration */
#include "button.h"
#include "fan.h"
#include "fusb302.h"
#include "gpio.h"
#include "i2c.h"
#include "i2c_bitbang.h"
#include "it83xx_pd.h"
#include "lid_switch.h"
#include "pca9675.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "switch.h"
#include "tablet_mode.h"
#include "uart.h"
#include "usb_pd_tbt.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#include "gpio_list.h" /* Must come after other header files. */

/******************************************************************************/
/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	[I2C_CHAN_FLASH] = {
		.name = "ec_flash",
		.port = IT83XX_I2C_CH_A,
		.kbps = 100,
		.scl = GPIO_EC_I2C_PROG_SCL,
		.sda = GPIO_EC_I2C_PROG_SDA,
	},
	[I2C_CHAN_BATT_CHG] = {
		.name = "batt_chg",
		.port = IT83XX_I2C_CH_B,
		.kbps = 100,
		.scl = GPIO_SMB_BS_CLK,
		.sda = GPIO_SMB_BS_DATA,
	},
	[I2C_CHAN_TYPEC_0] = {
		.name = "typec_0",
		.port = IT83XX_I2C_CH_C,
		.kbps = 400,
		.scl = GPIO_USBC_TCPC_I2C_CLK_P0,
		.sda = GPIO_USBC_TCPC_I2C_DATA_P0,
	},
	[I2C_CHAN_TYPEC_1] = {
		.name = "typec_1",
		.port = IT83XX_I2C_CH_F,
		.kbps = 400,
		.scl = GPIO_USBC_TCPC_I2C_CLK_P2,
		.sda = GPIO_USBC_TCPC_I2C_DATA_P2,
	},
#if defined(HAS_TASK_PD_C2)
	[I2C_CHAN_TYPEC_2] = {
		.name = "typec_2",
		.port = IT83XX_I2C_CH_E,
		.kbps = 400,
		.scl = GPIO_USBC_TCPC_I2C_CLK_P1,
		.sda = GPIO_USBC_TCPC_I2C_DATA_P1,
	},
	[I2C_CHAN_TYPEC_3] = {
		.name = "typec_3",
		.port = IT83XX_I2C_CH_D,
		.kbps = 400,
		.scl = GPIO_USBC_TCPC_I2C_CLK_P3,
		.sda = GPIO_USBC_TCPC_I2C_DATA_P3,
	},
#endif
};
BUILD_ASSERT(ARRAY_SIZE(i2c_ports) == I2C_CHAN_COUNT);
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

const struct i2c_port_t i2c_bitbang_ports[] = {
	[I2C_BITBANG_CHAN_BRD_ID] = {
		.name = "bitbang_brd_id",
		.port = IT83XX_I2C_CH_B,
		.kbps = 100,
		.scl = GPIO_SMB_BS_CLK,
		.sda = GPIO_SMB_BS_DATA,
		.drv = &bitbang_drv,
	},
	[I2C_BITBANG_CHAN_IOEX_0] = {
		.name = "bitbang_ioex_0",
		.port = IT83XX_I2C_CH_C,
		.kbps = 100,
		.scl = GPIO_USBC_TCPC_I2C_CLK_P0,
		.sda = GPIO_USBC_TCPC_I2C_DATA_P0,
		.drv = &bitbang_drv,
	},
};
BUILD_ASSERT(ARRAY_SIZE(i2c_bitbang_ports) == I2C_BITBANG_CHAN_COUNT);
const unsigned int i2c_bitbang_ports_used = ARRAY_SIZE(i2c_bitbang_ports);

/* USB-C TCPC Configuration */
const struct tcpc_config_t tcpc_config[] = {
	[TYPE_C_PORT_0] = {
		.bus_type = EC_BUS_TYPE_EMBEDDED,
		/* TCPC is embedded within EC so no i2c config needed */
		.drv = &it83xx_tcpm_drv,
	},
#if defined(HAS_TASK_PD_C1)
	[TYPE_C_PORT_1] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TYPEC_1,
			.addr_flags = I2C_ADDR_FUSB302_TCPC_AIC,
		},
		.drv = &fusb302_tcpm_drv,
	},
#endif
#if defined(HAS_TASK_PD_C2)
	[TYPE_C_PORT_2] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TYPEC_2,
			.addr_flags = I2C_ADDR_FUSB302_TCPC_AIC,
		},
		.drv = &fusb302_tcpm_drv,
	},
#endif
#if defined(HAS_TASK_PD_C3)
	[TYPE_C_PORT_3] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TYPEC_3,
			.addr_flags = I2C_ADDR_FUSB302_TCPC_AIC,
		},
		.drv = &fusb302_tcpm_drv,
	},
#endif
};
BUILD_ASSERT(ARRAY_SIZE(tcpc_config) == CONFIG_USB_PD_PORT_MAX_COUNT);
