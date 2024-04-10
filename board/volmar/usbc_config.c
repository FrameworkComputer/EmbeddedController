/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cbi.h"
#include "charge_ramp.h"
#include "charger.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "driver/bc12/pi3usb9201_public.h"
#include "driver/ppc/syv682x_public.h"
#include "driver/tcpm/ps8xxx_public.h"
#include "driver/tcpm/rt1715.h"
#include "driver/tcpm/tcpci.h"
#include "ec_commands.h"
#include "fw_config.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "ioexpander.h"
#include "system.h"
#include "task.h"
#include "task_id.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usbc_config.h"
#include "usbc_ppc.h"

#include <stdbool.h>
#include <stdint.h>

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

/* USBC TCPC configuration */
const struct tcpc_config_t tcpc_config[] = {
	[USBC_PORT_C0] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C0_C2_TCPC,
			.addr_flags = RT1715_I2C_ADDR_FLAGS,
		},
		.drv = &rt1715_tcpm_drv,
		.flags = TCPC_FLAGS_TCPCI_REV2_0 |
			 TCPC_FLAGS_TCPCI_REV2_0_NO_VSAFE0V,
	},
	[USBC_PORT_C1] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C1_TCPC,
			.addr_flags = PS8XXX_I2C_ADDR1_FLAGS,
		},
		.drv = &ps8xxx_tcpm_drv,
		.flags = TCPC_FLAGS_TCPCI_REV2_0 |
			 TCPC_FLAGS_TCPCI_REV2_0_NO_VSAFE0V,
	},
};
BUILD_ASSERT(ARRAY_SIZE(tcpc_config) == USBC_PORT_COUNT);
BUILD_ASSERT(CONFIG_USB_PD_PORT_MAX_COUNT == USBC_PORT_COUNT);

/******************************************************************************/
/* USB-A charging control */

const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_PP5000_USBA_R,
};
BUILD_ASSERT(ARRAY_SIZE(usb_port_enable) == USB_PORT_COUNT);

/******************************************************************************/

/* USBC PPC configuration */
struct ppc_config_t ppc_chips[] = {
	[USBC_PORT_C0] = {
		.i2c_port = I2C_PORT_USB_C0_C2_PPC,
		.i2c_addr_flags = SYV682X_ADDR0_FLAGS,
		.drv = &syv682x_drv,
	},
	[USBC_PORT_C1] = {
		/* Compatible with Silicon Mitus SM536A0 */
		.i2c_port = I2C_PORT_USB_C1_PPC,
		.i2c_addr_flags = SYV682X_ADDR0_FLAGS,
		.drv = &syv682x_drv,
	},
};
BUILD_ASSERT(ARRAY_SIZE(ppc_chips) == USBC_PORT_COUNT);

unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

/*
 * USB3 DB mux configuration - the top level mux still needs to be set
 * to the virtual_usb_mux_driver so the AP gets notified of mux changes
 * and updates the TCSS configuration on state changes.
 */
static const struct usb_mux_chain usbc1_usb3_db_retimer = {
	.mux =
		&(const struct usb_mux){
			.usb_port = USBC_PORT_C1,
			.driver = &tcpci_tcpm_usb_mux_driver,
			.hpd_update = &ps8xxx_tcpc_update_hpd_status,
		},
};

const struct usb_mux_chain usb_muxes[] = {
	[USBC_PORT_C0] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USBC_PORT_C0,
			.driver = &virtual_usb_mux_driver,
			.hpd_update = &virtual_hpd_update,
		},
	},
	[USBC_PORT_C1] = {
		.mux = &(const struct usb_mux) {
			/* PS8815 DB */
			.usb_port = USBC_PORT_C1,
			.driver = &virtual_usb_mux_driver,
			.hpd_update = &virtual_hpd_update,
		},
		.next = &usbc1_usb3_db_retimer,
	},
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == USBC_PORT_COUNT);

/* BC1.2 charger detect configuration */
const struct pi3usb9201_config_t pi3usb9201_bc12_chips[] = {
	[USBC_PORT_C0] = {
		.i2c_port = I2C_PORT_USB_C0_C2_BC12,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
	[USBC_PORT_C1] = {
		.i2c_port = I2C_PORT_USB_C1_BC12,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3usb9201_bc12_chips) == USBC_PORT_COUNT);

#ifdef CONFIG_CHARGE_RAMP_SW

/*
 * TODO(b/181508008): tune this threshold
 */

#define BC12_MIN_VOLTAGE 4400

/**
 * Return true if VBUS is too low
 */
int board_is_vbus_too_low(int port, enum chg_ramp_vbus_state ramp_state)
{
	int voltage;

	if (charger_get_vbus_voltage(port, &voltage))
		voltage = 0;

	if (voltage == 0) {
		CPRINTS("%s: must be disconnected", __func__);
		return 1;
	}

	if (voltage < BC12_MIN_VOLTAGE) {
		CPRINTS("%s: port %d: vbus %d lower than %d", __func__, port,
			voltage, BC12_MIN_VOLTAGE);
		return 1;
	}

	return 0;
}

#endif /* CONFIG_CHARGE_RAMP_SW */

void config_usb_db_type(void)
{
	enum ec_cfg_usb_db_type db_type = ec_cfg_usb_db_type();

	/*
	 * TODO(b/180434685): implement multiple DB types
	 */

	CPRINTS("Configured USB DB type number is %d", db_type);
}

void board_reset_pd_mcu(void)
{
	/*
	 * TODO(b/179648104): figure out correct timing
	 */

	gpio_set_level(GPIO_USB_C0_RT_RST_ODL, 0);
	gpio_set_level(GPIO_USB_C1_RT_RST_R_ODL, 0);

	/*
	 * delay for power-on to reset-off and min. assertion time
	 */

	crec_msleep(20);

	gpio_set_level(GPIO_USB_C0_RT_RST_ODL, 1);
	gpio_set_level(GPIO_USB_C1_RT_RST_R_ODL, 1);

	/* wait for chips to come up */

	crec_msleep(50);
}

static void board_tcpc_init(void)
{
	/* Don't reset TCPCs after initial reset */
	if (!system_jumped_late())
		board_reset_pd_mcu();

	/* Enable PPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_PPC_INT_ODL);

	/* Enable TCPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_C2_TCPC_INT_ODL);

	/* Enable BC1.2 interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_ODL);

	if (ec_cfg_usb_db_type() != DB_USB_ABSENT) {
		gpio_enable_interrupt(GPIO_USB_C1_PPC_INT_ODL);
		gpio_enable_interrupt(GPIO_USB_C1_TCPC_INT_ODL);
		gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_ODL);
	}
}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_CHIPSET);

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (gpio_get_level(GPIO_USB_C0_C2_TCPC_INT_ODL) == 0)
		status |= PD_STATUS_TCPC_ALERT_0 | PD_STATUS_TCPC_ALERT_2;

	if ((ec_cfg_usb_db_type() != DB_USB_ABSENT) &&
	    gpio_get_level(GPIO_USB_C1_TCPC_INT_ODL) == 0)
		status |= PD_STATUS_TCPC_ALERT_1;

	return status;
}

int ppc_get_alert_status(int port)
{
	if (port == USBC_PORT_C0)
		return gpio_get_level(GPIO_USB_C0_PPC_INT_ODL) == 0;
	else if ((port == USBC_PORT_C1) &&
		 (ec_cfg_usb_db_type() != DB_USB_ABSENT))
		return gpio_get_level(GPIO_USB_C1_PPC_INT_ODL) == 0;
	return 0;
}

void tcpc_alert_event(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_C2_TCPC_INT_ODL:
		schedule_deferred_pd_interrupt(USBC_PORT_C0);
		break;
	case GPIO_USB_C1_TCPC_INT_ODL:
		if (ec_cfg_usb_db_type() == DB_USB_ABSENT)
			break;
		schedule_deferred_pd_interrupt(USBC_PORT_C1);
		break;
	default:
		break;
	}
}

void bc12_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_BC12_INT_ODL:
		usb_charger_task_set_event(0, USB_CHG_EVENT_BC12);
		break;
	case GPIO_USB_C1_BC12_INT_ODL:
		if (ec_cfg_usb_db_type() == DB_USB_ABSENT)
			break;
		usb_charger_task_set_event(1, USB_CHG_EVENT_BC12);
		break;
	default:
		break;
	}
}

void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_PPC_INT_ODL:
		syv682x_interrupt(USBC_PORT_C0);
		break;
	case GPIO_USB_C1_PPC_INT_ODL:
		switch (ec_cfg_usb_db_type()) {
		case DB_USB_ABSENT:
		case DB_USB_ABSENT2:
			break;
		case DB_USB3_PS8815:
			syv682x_interrupt(USBC_PORT_C1);
			break;
		}
		break;
	default:
		break;
	}
}

__override bool board_is_dts_port(int port)
{
	return port == USBC_PORT_C0;
}
