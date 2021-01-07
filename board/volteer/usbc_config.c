/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Volteer family-specific USB-C configuration */
#include "common.h"
#include "cbi_ec_fw_config.h"
#include "usbc_config.h"
#include "usbc_ppc.h"
#include "usb_mux.h"
#include "driver/bc12/pi3usb9201_public.h"
#include "driver/ppc/sn5s330_public.h"
#include "driver/ppc/syv682x_public.h"
#include "driver/tcpm/ps8xxx_public.h"
#include "driver/tcpm/rt1715_public.h"
#include "driver/tcpm/tusb422_public.h"
#include "driver/tcpm/tcpci.h"

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

/* USBC TCPC configuration for USB3 daughter board */
static const struct tcpc_config_t tcpc_config_p1_usb3 = {
	.bus_type = EC_BUS_TYPE_I2C,
	.i2c_info = {
		.port = I2C_PORT_USB_C1,
		.addr_flags = PS8751_I2C_ADDR1_FLAGS,
	},
	.flags = TCPC_FLAGS_TCPCI_REV2_0 | TCPC_FLAGS_TCPCI_REV2_0_NO_VSAFE0V,
	.drv = &ps8xxx_tcpm_drv,
};

/*
 * USB3 DB mux configuration - the top level mux still needs to be set to the
 * virtual_usb_mux_driver so the AP gets notified of mux changes and updates
 * the TCSS configuration on state changes.
 */
static const struct usb_mux usbc1_usb3_db_retimer = {
	.usb_port = USBC_PORT_C1,
	.driver = &tcpci_tcpm_usb_mux_driver,
	.hpd_update = &ps8xxx_tcpc_update_hpd_status,
	.next_mux = NULL,
};

static const struct usb_mux mux_config_p1_usb3_active = {
	.usb_port = USBC_PORT_C1,
	.driver = &virtual_usb_mux_driver,
	.hpd_update = &virtual_hpd_update,
	.next_mux = &usbc1_usb3_db_retimer,
};

static const struct usb_mux mux_config_p1_usb3_passive = {
	.usb_port = USBC_PORT_C1,
	.driver = &virtual_usb_mux_driver,
	.hpd_update = &virtual_hpd_update,
};

/*
 * Set up support for the USB3 daughterboard:
 *   Parade PS8815 TCPC (integrated retimer)
 *   Diodes PI3USB9201 BC 1.2 chip (same as USB4 board)
 *   Silergy SYV682A PPC (same as USB4 board)
 *   Virtual mux with stacked retimer
 */
static void config_db_usb3_active(void)
{
	tcpc_config[USBC_PORT_C1] = tcpc_config_p1_usb3;
	usb_muxes[USBC_PORT_C1] = mux_config_p1_usb3_active;
}

/*
 * Set up support for the passive USB3 daughterboard:
 *   TUSB422 TCPC (already the default)
 *   PI3USB9201 BC 1.2 chip (already the default)
 *   Silergy SYV682A PPC (already the default)
 *   Virtual mux without stacked retimer
 */

static void config_db_usb3_passive(void)
{
	usb_muxes[USBC_PORT_C1] = mux_config_p1_usb3_passive;
}

static void config_port_discrete_tcpc(int port)
{
	/*
	 * Support 2 Pin-to-Pin compatible parts: TUSB422 and RT1715, for
	 * simplicity allow either and decide at runtime which we are using.
	 * Default to TUSB422, and switch to RT1715 if it is on the I2C bus and
	 * the VID matches.
	 */

	int regval;

	if (i2c_read16(port ? I2C_PORT_USB_C1 : I2C_PORT_USB_C0,
		       RT1715_I2C_ADDR_FLAGS, TCPC_REG_VENDOR_ID,
		       &regval) == EC_SUCCESS) {
		if (regval == RT1715_VENDOR_ID) {
			CPRINTS("C%d: RT1715 detected", port);
			tcpc_config[port].i2c_info.addr_flags =
				RT1715_I2C_ADDR_FLAGS;
			tcpc_config[port].drv = &rt1715_tcpm_drv;
			return;
		}
	}
	CPRINTS("C%d: Default to TUSB422", port);
}

static const char *db_type_prefix = "USB DB type: ";
void config_usb3_db_type(void)
{
	enum ec_cfg_usb_db_type usb_db = ec_cfg_usb_db_type();

	config_port_discrete_tcpc(0);
	switch (usb_db) {
	case DB_USB_ABSENT:
		CPRINTS("%sNone", db_type_prefix);
		break;
	case DB_USB4_GEN2:
		config_port_discrete_tcpc(1);
		CPRINTS("%sUSB4 Gen1/2", db_type_prefix);
		break;
	case DB_USB4_GEN3:
		config_port_discrete_tcpc(1);
		CPRINTS("%sUSB4 Gen3", db_type_prefix);
		break;
	case DB_USB3_ACTIVE:
		config_db_usb3_active();
		CPRINTS("%sUSB3 Active", db_type_prefix);
		break;
	case DB_USB3_PASSIVE:
		config_db_usb3_passive();
		config_port_discrete_tcpc(1);
		CPRINTS("%sUSB3 Passive", db_type_prefix);
		break;
	default:
		CPRINTS("%sID %d not supported", db_type_prefix, usb_db);
	}
}

/******************************************************************************/
/* USBC PPC configuration */
struct ppc_config_t ppc_chips[] = {
	[USBC_PORT_C0] = {
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr_flags = SN5S330_ADDR0_FLAGS,
		.drv = &sn5s330_drv,
	},
	[USBC_PORT_C1] = {
		.i2c_port = I2C_PORT_USB_C1,
		.i2c_addr_flags = SYV682X_ADDR0_FLAGS,
		.drv = &syv682x_drv,
	},
};
BUILD_ASSERT(ARRAY_SIZE(ppc_chips) == USBC_PORT_COUNT);
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

/******************************************************************************/
/* PPC support routines */
void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_PPC_INT_ODL:
		sn5s330_interrupt(USBC_PORT_C0);
		break;
	case GPIO_USB_C1_PPC_INT_ODL:
		syv682x_interrupt(USBC_PORT_C1);
	default:
		break;
	}
}

/******************************************************************************/
/* USBC TCPC configuration */
struct tcpc_config_t tcpc_config[] = {
	[USBC_PORT_C0] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C0,
			.addr_flags = TUSB422_I2C_ADDR_FLAGS,
		},
		.drv = &tusb422_tcpm_drv,
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

/******************************************************************************/
/* BC1.2 charger detect configuration */
const struct pi3usb9201_config_t pi3usb9201_bc12_chips[] = {
	[USBC_PORT_C0] = {
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
	[USBC_PORT_C1] = {
		.i2c_port = I2C_PORT_USB_C1,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3usb9201_bc12_chips) == USBC_PORT_COUNT);
