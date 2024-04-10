/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Volteer family-specific USB-C configuration */
#include "cbi_ec_fw_config.h"
#include "common.h"
#include "driver/bc12/pi3usb9201_public.h"
#include "driver/ppc/sn5s330_public.h"
#include "driver/ppc/syv682x_public.h"
#include "driver/retimer/bb_retimer_public.h"
#include "driver/tcpm/ps8xxx_public.h"
#include "driver/tcpm/rt1715_public.h"
#include "driver/tcpm/tcpci.h"
#include "driver/tcpm/tusb422_public.h"
#include "gpio.h"
#include "hooks.h"
#include "system.h"
#include "timer.h"
#include "usb_mux.h"
#include "usbc_config.h"
#include "usbc_ppc.h"

/* Disable debug messages to save flash space */
#define CPRINTS(format, args...)

/* USBC TCPC configuration for USB3 daughter board */
static const struct tcpc_config_t tcpc_config_p1_usb3 = {
	.bus_type = EC_BUS_TYPE_I2C,
	.i2c_info = {
		.port = I2C_PORT_USB_C1,
		.addr_flags = PS8XXX_I2C_ADDR1_FLAGS,
	},
	.flags = TCPC_FLAGS_TCPCI_REV2_0 | TCPC_FLAGS_TCPCI_REV2_0_NO_VSAFE0V,
	.drv = &ps8xxx_tcpm_drv,
};

/*
 * USB3 DB mux configuration - the top level mux still needs to be set to the
 * virtual_usb_mux_driver so the AP gets notified of mux changes and updates
 * the TCSS configuration on state changes.
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

static const struct usb_mux_chain mux_config_p1_usb3_active = {
	.mux =
		&(const struct usb_mux){
			.usb_port = USBC_PORT_C1,
			.driver = &virtual_usb_mux_driver,
			.hpd_update = &virtual_hpd_update,
		},
	.next = &usbc1_usb3_db_retimer,
};

static const struct usb_mux_chain mux_config_p1_usb3_passive = {
	.mux =
		&(const struct usb_mux){
			.usb_port = USBC_PORT_C1,
			.driver = &virtual_usb_mux_driver,
			.hpd_update = &virtual_hpd_update,
		}
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

void config_usb3_db_type(void)
{
	enum ec_cfg_usb_db_type usb_db = ec_cfg_usb_db_type();

	config_port_discrete_tcpc(0);
	switch (usb_db) {
	case DB_USB_ABSENT:
		CPRINTS("USB DB Type: None");
		break;
	case DB_USB4_GEN2:
		config_port_discrete_tcpc(1);
		CPRINTS("USB DB Type: USB4 Gen1/2");
		break;
	case DB_USB4_GEN3:
		config_port_discrete_tcpc(1);
		CPRINTS("USB DB Type: USB4 Gen3");
		break;
	case DB_USB3_ACTIVE:
		config_db_usb3_active();
		CPRINTS("USB DB Type: USB3 Active");
		break;
	case DB_USB3_PASSIVE:
		config_db_usb3_passive();
		config_port_discrete_tcpc(1);
		CPRINTS("USB DB Type: USB3 Passive");
		break;
	default:
		CPRINTS("USB DB Type: ID %d not supported", usb_db);
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
		.frs_en = GPIO_USB_C1_FRS_EN,
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
		break;
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
/* USB-A charging control */

const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_PP5000_USBA,
};

/******************************************************************************/
/* USBC mux configuration - Tiger Lake includes internal mux */
struct usb_mux_chain usbc1_tcss_usb_mux = {
	.mux =
		&(const struct usb_mux){
			.usb_port = USBC_PORT_C1,
			.driver = &virtual_usb_mux_driver,
			.hpd_update = &virtual_hpd_update,
		}
};

struct usb_mux_chain usb_muxes[] = {
	[USBC_PORT_C0] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USBC_PORT_C0,
			.driver = &virtual_usb_mux_driver,
			.hpd_update = &virtual_hpd_update,
		},
	},
	[USBC_PORT_C1] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USBC_PORT_C1,
			.driver = &bb_usb_retimer,
			.hpd_update = bb_retimer_hpd_update,
			.i2c_port = I2C_PORT_USB_1_MIX,
			.i2c_addr_flags = USBC_PORT_C1_BB_RETIMER_I2C_ADDR,
		},
		.next = &usbc1_tcss_usb_mux,
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

__override enum tbt_compat_cable_speed board_get_max_tbt_speed(int port)
{
	enum ec_cfg_usb_db_type usb_db = ec_cfg_usb_db_type();

	if (port == USBC_PORT_C1) {
		if (usb_db == DB_USB4_GEN2) {
			/*
			 * Older boards violate 205mm trace length prior
			 * to connection to the re-timer and only support up
			 * to GEN2 speeds.
			 */
			return TBT_SS_U32_GEN1_GEN2;
		} else if (usb_db == DB_USB4_GEN3) {
			return TBT_SS_TBT_GEN3;
		}
	}

	/*
	 * Thunderbolt-compatible mode not supported
	 *
	 * TODO (b/147726366): All the USB-C ports need to support same speed.
	 * Need to fix once USB-C feature set is known for Volteer.
	 */
	return TBT_SS_RES_0;
}

__override bool board_is_tbt_usb4_port(int port)
{
	enum ec_cfg_usb_db_type usb_db = ec_cfg_usb_db_type();

	/*
	 * Volteer reference design only supports TBT & USB4 on port 1
	 * if the USB4 DB is present.
	 *
	 * TODO (b/147732807): All the USB-C ports need to support same
	 * features. Need to fix once USB-C feature set is known for Volteer.
	 */
	return ((port == USBC_PORT_C1) &&
		((usb_db == DB_USB4_GEN2) || (usb_db == DB_USB4_GEN3)));
}

static void ps8815_reset(void)
{
	int val;

	gpio_set_level(GPIO_USB_C1_RT_RST_ODL, 0);
	crec_msleep(GENERIC_MAX(PS8XXX_RESET_DELAY_MS,
				PS8815_PWR_H_RST_H_DELAY_MS));
	gpio_set_level(GPIO_USB_C1_RT_RST_ODL, 1);
	crec_msleep(PS8815_FW_INIT_DELAY_MS);

	/*
	 * b/144397088
	 * ps8815 firmware 0x01 needs special configuration
	 */

	CPRINTS("%s: patching ps8815 registers", __func__);

	if (i2c_read8(I2C_PORT_USB_C1, PS8XXX_I2C_ADDR1_P2_FLAGS, 0x0f, &val) ==
	    EC_SUCCESS)
		CPRINTS("ps8815: reg 0x0f was %02x", val);

	if (i2c_write8(I2C_PORT_USB_C1, PS8XXX_I2C_ADDR1_P2_FLAGS, 0x0f,
		       0x31) == EC_SUCCESS)
		CPRINTS("ps8815: reg 0x0f set to 0x31");

	if (i2c_read8(I2C_PORT_USB_C1, PS8XXX_I2C_ADDR1_P2_FLAGS, 0x0f, &val) ==
	    EC_SUCCESS)
		CPRINTS("ps8815: reg 0x0f now %02x", val);
}

void board_reset_pd_mcu(void)
{
	enum ec_cfg_usb_db_type usb_db = ec_cfg_usb_db_type();

	/* No reset available for TCPC on port 0 */
	/* Daughterboard specific reset for port 1 */
	if (usb_db == DB_USB3_ACTIVE) {
		ps8815_reset();
		usb_mux_hpd_update(USBC_PORT_C1,
				   USB_PD_MUX_HPD_LVL_DEASSERTED |
					   USB_PD_MUX_HPD_IRQ_DEASSERTED);
	}
}

static void board_tcpc_init(void)
{
	/* Don't reset TCPCs after initial reset */
	if (!system_jumped_late())
		board_reset_pd_mcu();

	/* Enable PPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_PPC_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_PPC_INT_ODL);

	/* Enable TCPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_TCPC_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_TCPC_INT_ODL);

#ifndef CONFIG_ZEPHYR
	/* Enable BC1.2 interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_ODL);
#endif /* !CONFIG_ZEPHYR */
}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_CHIPSET);

#ifndef CONFIG_ZEPHYR
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
#endif /* !CONFIG_ZEPHYR */

/******************************************************************************/
/* TCPC support routines */
uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	/*
	 * Check which port has the ALERT line set
	 */
	if (!gpio_get_level(GPIO_USB_C0_TCPC_INT_ODL))
		status |= PD_STATUS_TCPC_ALERT_0;
	if (!gpio_get_level(GPIO_USB_C1_TCPC_INT_ODL))
		status |= PD_STATUS_TCPC_ALERT_1;

	return status;
}

/******************************************************************************/

int ppc_get_alert_status(int port)
{
	if (port == USBC_PORT_C0)
		return gpio_get_level(GPIO_USB_C0_PPC_INT_ODL) == 0;
	else
		return gpio_get_level(GPIO_USB_C1_PPC_INT_ODL) == 0;
}
