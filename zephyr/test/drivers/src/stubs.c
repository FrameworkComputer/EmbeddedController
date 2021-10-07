/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "bc12/pi3usb9201_public.h"
#include "charge_ramp.h"
#include "charger.h"
#include "charger/isl923x_public.h"
#include "charger/isl9241_public.h"
#include "config.h"
#include "i2c/i2c.h"
#include "power.h"
#include "ppc/sn5s330_public.h"
#include "ppc/syv682x_public.h"
#include "retimer/bb_retimer_public.h"
#include "stubs.h"
#include "tcpm/tcpci.h"
#include "tcpm/tusb422_public.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"

/* All of these definitions are just to get the test to link. None of these
 * functions are useful or behave as they should. Please remove them once the
 * real code is able to be added.  Most of the things here should either be
 * in emulators or in the native_posix board-specific code or part of the
 * device tree.
 */

/* BC1.2 charger detect configuration */
const struct pi3usb9201_config_t pi3usb9201_bc12_chips[] = {
	[USBC_PORT_C0] = {
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
	[USBC_PORT_C1] = {
		.i2c_port = I2C_PORT_USB_C1,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_1_FLAGS,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3usb9201_bc12_chips) == USBC_PORT_COUNT);

/* Charger Chip Configuration */
const struct charger_config_t chg_chips[] = {
#ifdef CONFIG_PLATFORM_EC_CHARGER_ISL9241
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL9241_ADDR_FLAGS,
		.drv = &isl9241_drv,
	},
#endif
#ifdef CONFIG_PLATFORM_EC_CHARGER_ISL9238
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
#endif
};

uint8_t board_get_charger_chip_count(void)
{
	return ARRAY_SIZE(chg_chips);
}

const struct board_batt_params board_battery_info[] = {
	/* LGC\011 L17L3PB0 Battery Information */
	/*
	 * Battery info provided by ODM on b/143477210, comment #11
	 */
	[BATTERY_LGC011] = {
		.fuel_gauge = {
			.manuf_name = "LGC",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x10, 0x10 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x6000,
				.disconnect_val = 0x6000,
			}
		},
		.batt_info = {
			.voltage_max		= TARGET_WITH_MARGIN(13200, 5),
			.voltage_normal		= 11550, /* mV */
			.voltage_min		= 9000, /* mV */
			.precharge_current	= 256,	/* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 45,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= 0,
			.discharging_max_c	= 75,
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_LGC011;

int board_set_active_charge_port(int port)
{
	return EC_SUCCESS;
}

int board_is_vbus_too_low(int port, enum chg_ramp_vbus_state ramp_state)
{
	return 0;
}

void board_set_charge_limit(int port, int supplier, int charge_ma, int max_ma,
			    int charge_mv)
{
}

struct tcpc_config_t tcpc_config[] = {
	[USBC_PORT_C0] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C0,
			.addr_flags = DT_REG_ADDR(DT_NODELABEL(tcpci_emul)),
		},
		.drv = &tcpci_tcpm_drv,
	},
	[USBC_PORT_C1] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C1,
			.addr_flags = TUSB422_I2C_ADDR_FLAGS,
		},
		.drv = &tusb422_tcpm_drv,
	},
};
BUILD_ASSERT(ARRAY_SIZE(tcpc_config) == USBC_PORT_COUNT);
BUILD_ASSERT(CONFIG_USB_PD_PORT_MAX_COUNT == USBC_PORT_COUNT);

int board_is_sourcing_vbus(int port)
{
	return 0;
}

struct usb_mux usbc0_virtual_usb_mux = {
	.usb_port = USBC_PORT_C0,
	.driver = &virtual_usb_mux_driver,
	.hpd_update = &virtual_hpd_update,
};

struct usb_mux usbc1_virtual_usb_mux = {
	.usb_port = USBC_PORT_C1,
	.driver = &virtual_usb_mux_driver,
	.hpd_update = &virtual_hpd_update,
};

struct usb_mux usb_muxes[] = {
	[USBC_PORT_C0] = {
		.usb_port = USBC_PORT_C0,
		.driver = &tcpci_tcpm_usb_mux_driver,
		.next_mux = &usbc0_virtual_usb_mux,
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr_flags = DT_REG_ADDR(DT_NODELABEL(tcpci_emul)),
	},
	[USBC_PORT_C1] = {
		.usb_port = USBC_PORT_C1,
		.driver = &bb_usb_retimer,
		.hpd_update = bb_retimer_hpd_update,
		.next_mux = &usbc1_virtual_usb_mux,
		.i2c_port = I2C_PORT_USB_C1,
		.i2c_addr_flags = DT_REG_ADDR(DT_NODELABEL(
					usb_c1_bb_retimer_emul)),
	},
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == USBC_PORT_COUNT);

struct bb_usb_control bb_controls[] = {
	[USBC_PORT_C0] = {
		/* USB-C port 0 doesn't have a retimer */
	},
	[USBC_PORT_C1] = {
		.usb_ls_en_gpio = GPIO_USB_C1_LS_EN,
		.retimer_rst_gpio = GPIO_USB_C1_RT_RST_ODL,
	},
};
BUILD_ASSERT(ARRAY_SIZE(bb_controls) == USBC_PORT_COUNT);

void pd_power_supply_reset(int port)
{
}

int pd_check_vconn_swap(int port)
{
	return 0;
}

int pd_set_power_supply_ready(int port)
{
	return EC_SUCCESS;
}

/* USBC PPC configuration */
struct ppc_config_t ppc_chips[] = {
	[USBC_PORT_C0] = {
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr_flags = SN5S330_ADDR0_FLAGS,
		.drv = &sn5s330_drv,
	},
	[USBC_PORT_C1] = {
		.i2c_port = I2C_PORT_USB_C1,
		.i2c_addr_flags = SYV682X_ADDR1_FLAGS,
		/* TODO(b/190519131): Add FRS GPIO, test FRS */
		.drv = &syv682x_drv,
	},
};
BUILD_ASSERT(ARRAY_SIZE(ppc_chips) == USBC_PORT_COUNT);
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
}

uint16_t tcpc_get_alert_status(void)
{
	return 0;
}

enum power_state power_chipset_init(void)
{
	return POWER_G3;
}

enum power_state mock_state = POWER_G3;

void set_mock_power_state(enum power_state state)
{
	mock_state = state;
	task_wake(TASK_ID_CHIPSET);
}

enum power_state power_handle_state(enum power_state state)
{
	return mock_state;
}

void chipset_reset(enum chipset_reset_reason reason)
{
}

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
}

/* Power signals list. Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {};
