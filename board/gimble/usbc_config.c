/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <stdbool.h>

#include "battery.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "driver/bc12/pi3usb9201_public.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/ppc/syv682x_public.h"
#include "driver/tcpm/nct38xx.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/ps8xxx_public.h"
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
#include "usbc_config.h"
#include "usbc_ppc.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

/* USBC TCPC configuration */
const struct tcpc_config_t tcpc_config[] = {
	[USBC_PORT_C0] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C0_TCPC,
			.addr_flags = NCT38XX_I2C_ADDR1_1_FLAGS,
		},
		.drv = &nct38xx_tcpm_drv,
		.flags = TCPC_FLAGS_TCPCI_REV2_0,
	},
	[USBC_PORT_C1] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C1_TCPC,
			.addr_flags = PS8XXX_I2C_ADDR1_FLAGS,
		},
		.drv = &ps8xxx_tcpm_drv,
		.flags = TCPC_FLAGS_TCPCI_REV2_0 |
			 TCPC_FLAGS_TCPCI_REV2_0_NO_VSAFE0V |
			 TCPC_FLAGS_CONTROL_VCONN |
			 TCPC_FLAGS_CONTROL_FRS,
	},
};
BUILD_ASSERT(ARRAY_SIZE(tcpc_config) == USBC_PORT_COUNT);
BUILD_ASSERT(CONFIG_USB_PD_PORT_MAX_COUNT == USBC_PORT_COUNT);

/* USBC PPC configuration */
struct ppc_config_t ppc_chips[] = {
	[USBC_PORT_C0] = {
		.i2c_port = I2C_PORT_USB_C0_PPC,
		.i2c_addr_flags = SYV682X_ADDR0_FLAGS,
		.frs_en = IOEX_USB_C0_FRS_EN,
		.drv = &syv682x_drv,
	},
	[USBC_PORT_C1] = {
		/* Compatible with Silicon Mitus SM536A0 */
		.i2c_port = I2C_PORT_USB_C1_PPC,
		.i2c_addr_flags = NX20P3483_ADDR2_FLAGS,
		.drv = &nx20p348x_drv,
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
	.next = NULL,
};

/* USBC mux configuration - Alder Lake includes internal mux */
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
		.i2c_port = I2C_PORT_USB_C0_BC12,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
	[USBC_PORT_C1] = {
		.i2c_port = I2C_PORT_USB_C1_BC12,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3usb9201_bc12_chips) == USBC_PORT_COUNT);

/*
 * ioex_init() is normally called before we take the TCPCs out of
 * reset, so we need to start in disabled mode, then explicitly
 * call ioex_init().
 */

struct ioexpander_config_t ioex_config[] = {
	[IOEX_C0_NCT38XX] = {
		.i2c_host_port = I2C_PORT_USB_C0_TCPC,
		.i2c_addr_flags = NCT38XX_I2C_ADDR1_1_FLAGS,
		.drv = &nct38xx_ioexpander_drv,
		.flags = IOEX_FLAGS_DEFAULT_INIT_DISABLED,
	},
};
BUILD_ASSERT(ARRAY_SIZE(ioex_config) == CONFIG_IO_EXPANDER_PORT_COUNT);

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
	/* Port0 */
	gpio_set_level(GPIO_USB_C0_TCPC_RST_ODL, 0);

	if (battery_hw_present())
		gpio_set_level(GPIO_USB_C1_RT_RST_R_ODL, 0);

	msleep(GENERIC_MAX(PS8XXX_RESET_DELAY_MS, PS8815_PWR_H_RST_H_DELAY_MS));

	gpio_set_level(GPIO_USB_C0_TCPC_RST_ODL, 1);
	gpio_set_level(GPIO_USB_C1_RT_RST_R_ODL, 1);

	/* wait for chips to come up */
	msleep(PS8815_FW_INIT_DELAY_MS);
}

static void board_tcpc_init(void)
{
	int i;

	/* Don't reset TCPCs after initial reset */
	if (!system_jumped_late()) {
		board_reset_pd_mcu();

		for (i = 0; i < CONFIG_IO_EXPANDER_PORT_COUNT; ++i)
			ioex_init(i);
	}

	/* Enable PPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_PPC_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_PPC_INT_ODL);

	/* Enable TCPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_TCPC_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_TCPC_INT_ODL);

	/* Enable BC1.2 interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_ODL);
}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_CHIPSET);

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (gpio_get_level(GPIO_USB_C0_TCPC_INT_ODL) == 0)
		status |= PD_STATUS_TCPC_ALERT_0;

	if (gpio_get_level(GPIO_USB_C1_TCPC_INT_ODL) == 0)
		status |= PD_STATUS_TCPC_ALERT_1;

	return status;
}

int ppc_get_alert_status(int port)
{
	if (port == USBC_PORT_C0)
		return gpio_get_level(GPIO_USB_C0_PPC_INT_ODL) == 0;
	else if (port == USBC_PORT_C1)
		return gpio_get_level(GPIO_USB_C1_PPC_INT_ODL) == 0;
	return 0;
}

void tcpc_alert_event(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_TCPC_INT_ODL:
		schedule_deferred_pd_interrupt(USBC_PORT_C0);
		break;
	case GPIO_USB_C1_TCPC_INT_ODL:
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
		nx20p348x_interrupt(USBC_PORT_C1);
		break;
	default:
		break;
	}
}

void retimer_interrupt(enum gpio_signal signal)
{
	/*
	 * TODO(b/179513527): add USB-C support
	 */
}

__override bool board_is_dts_port(int port)
{
	return port == USBC_PORT_C0;
}
