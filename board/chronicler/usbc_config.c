/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Volteer family-specific USB-C configuration */
#include "common.h"
#include "cbi_ec_fw_config.h"
#include "gpio.h"
#include "hooks.h"
#include "system.h"
#include "timer.h"
#include "usbc_config.h"
#include "usbc_ppc.h"
#include "usb_mux.h"
#include "driver/bc12/pi3usb9201_public.h"
#include "driver/ppc/sn5s330_public.h"
#include "driver/ppc/syv682x_public.h"
#include "driver/retimer/bb_retimer_public.h"
#include "driver/retimer/ps8811.h"
#include "driver/tcpm/ps8xxx_public.h"
#include "driver/tcpm/rt1715_public.h"
#include "driver/tcpm/tusb422_public.h"
#include "driver/tcpm/tcpci.h"

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

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
	default:
		break;
	}
}

/******************************************************************************/
/* USBC TCPC configuration */
const struct tcpc_config_t tcpc_config[] = {
	[USBC_PORT_C0] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C0,
			.addr_flags = RT1715_I2C_ADDR_FLAGS,
		},
		.drv = &rt1715_tcpm_drv,
	},
	[USBC_PORT_C1] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C1,
			.addr_flags = PS8751_I2C_ADDR1_FLAGS,
		},
		.flags = TCPC_FLAGS_TCPCI_REV2_0 |
			TCPC_FLAGS_TCPCI_REV2_0_NO_VSAFE0V,
		.drv = &ps8xxx_tcpm_drv,
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
const struct usb_mux usb_muxes[] = {
	[USBC_PORT_C0] = {
		.usb_port = USBC_PORT_C0,
		.driver = &virtual_usb_mux_driver,
		.hpd_update = &virtual_hpd_update,
	},
	[USBC_PORT_C1] = {
		.usb_port = USBC_PORT_C1,
		.driver = &virtual_usb_mux_driver,
		.hpd_update = &virtual_hpd_update,
		.next_mux = &usbc1_usb3_db_retimer,
	},
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == USBC_PORT_COUNT);

static void ps8815_reset(void)
{
	int val;

	gpio_set_level(GPIO_USB_C1_RT_RST_ODL, 0);
	msleep(GENERIC_MAX(PS8XXX_RESET_DELAY_MS,
			   PS8815_PWR_H_RST_H_DELAY_MS));
	gpio_set_level(GPIO_USB_C1_RT_RST_ODL, 1);
	msleep(PS8815_FW_INIT_DELAY_MS);

	/*
	 * b/144397088
	 * ps8815 firmware 0x01 needs special configuration
	 */

	CPRINTS("%s: patching ps8815 registers", __func__);

	if (i2c_read8(I2C_PORT_USB_C1,
		      PS8751_I2C_ADDR1_P2_FLAGS, 0x0f, &val) == EC_SUCCESS)
		CPRINTS("ps8815: reg 0x0f was %02x", val);

	if (i2c_write8(I2C_PORT_USB_C1,
		       PS8751_I2C_ADDR1_P2_FLAGS, 0x0f, 0x31) == EC_SUCCESS)
		CPRINTS("ps8815: reg 0x0f set to 0x31");

	if (i2c_read8(I2C_PORT_USB_C1,
		      PS8751_I2C_ADDR1_P2_FLAGS, 0x0f, &val) == EC_SUCCESS)
		CPRINTS("ps8815: reg 0x0f now %02x", val);
}

static void ps8815_setup_eq(void)
{
	int rv;
	const int port = tcpc_config[USBC_PORT_C1].i2c_info.port;
	const int addr = tcpc_config[USBC_PORT_C1].i2c_info.addr_flags;

	/* TX1 EQ 19db / TX2 EQ 19db */
	rv = i2c_write8(port, addr, 0x20, 0x77);

	/* RX1 EQ 12db / RX2 EQ 13db */
	rv |= i2c_write8(port, addr, 0x22, 0x32);

	/* Swing level for upstream port output */
	rv |= i2c_write8(port, addr, 0xc4, 0x03);

	if (rv)
		CPRINTS("%s fail!", __func__);
}

static void ps8811_setup_eq(void)
{
	int rv;
	const int port = I2C_PORT_USB_1_MIX;
	const int addr = PS8811_I2C_ADDR_FLAGS0 + PS8811_REG_PAGE1;

	/* AEQ 12db */
	rv = i2c_write8(port, addr, 0x01, 0x26);
	/* ADE 2.1db */
	rv |= i2c_write8(port, addr, 0x02, 0x60);
	/* BEQ 10.5db */
	rv |= i2c_write8(port, addr, 0x05, 0x16);
	/* BDE 2.1db */
	rv |= i2c_write8(port, addr, 0x06, 0x63);
	/* Channel A swing level */
	rv |= i2c_write8(port, addr, 0x66, 0x20);
	/* Channel B swing level */
	rv |= i2c_write8(port, addr, 0xa4, 0x03);
	/* PS level foe B channel */
	rv |= i2c_write8(port, addr, 0xa5, 0x83);
	/* DE level foe B channel */
	rv |= i2c_write8(port, addr, 0xa6, 0x14);

	if (rv)
		CPRINTS("%s fail!", __func__);
}

/* Called on AP S5 -> S0 transition */
void board_ps8xxx_init(void)
{
	CPRINTS("%s", __func__);
	/*
	 * Adjust USB3 settings to improve signal integrity.
	 * See b/194985848.
	 */
	ps8815_setup_eq();
	ps8811_setup_eq();
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_ps8xxx_init, HOOK_PRIO_LAST);

void board_reset_pd_mcu(void)
{
	/* No reset available for TCPC on port 0 */
	/* Daughterboard specific reset for port 1 */
	ps8815_reset();
	usb_mux_hpd_update(USBC_PORT_C1, 0, 0);
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

	/* Enable BC1.2 interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_ODL);
}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_CHIPSET);

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
