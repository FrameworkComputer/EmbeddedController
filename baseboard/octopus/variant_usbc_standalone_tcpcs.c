/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common code for VARIANT_OCTOPUS_USBC_STANDALONE_TCPCS configuration */

#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "driver/ppc/nx20p3483.h"
#include "driver/tcpm/anx7447.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/tcpci.h"
#include "driver/tcpm/tcpm.h"
#include "gpio.h"
#include "hooks.h"
#include "system.h"
#include "tcpci.h"
#include "usb_mux.h"
#include "usbc_ppc.h"
#include "util.h"

#define USB_PD_PORT_ANX7447	0
#define USB_PD_PORT_PS8751	1

/******************************************************************************/
/* USB-C TPCP Configuration */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
	[USB_PD_PORT_ANX7447] = {
		.i2c_host_port = I2C_PORT_TCPC0,
		.i2c_slave_addr = AN7447_TCPC0_I2C_ADDR,
		.drv = &anx7447_tcpm_drv,
		.pol = TCPC_ALERT_ACTIVE_LOW,
	},
	[USB_PD_PORT_PS8751] = {
		.i2c_host_port = I2C_PORT_TCPC1,
		.i2c_slave_addr = PS8751_I2C_ADDR1,
		.drv = &ps8xxx_tcpm_drv,
		.pol = TCPC_ALERT_ACTIVE_LOW,
	},
};

/******************************************************************************/
/* USB-C MUX Configuration */

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_COUNT] = {
	[USB_PD_PORT_ANX7447] = {
		.port_addr = USB_PD_PORT_ANX7447,
		.driver = &anx7447_usb_mux_driver,
		.hpd_update = &anx7447_tcpc_update_hpd_status,
	},
	[USB_PD_PORT_PS8751] = {
		.port_addr = USB_PD_PORT_PS8751,
		.driver = &tcpci_tcpm_usb_mux_driver,
		.hpd_update = &ps8xxx_tcpc_update_hpd_status,
	}
};

/******************************************************************************/
/* USB-C PPC Configuration */
const struct ppc_config_t ppc_chips[CONFIG_USB_PD_PORT_COUNT] = {
	[USB_PD_PORT_ANX7447] = {
		.i2c_port = I2C_PORT_TCPC0,
		.i2c_addr = NX20P3483_ADDR2,
		.drv = &nx20p3483_drv,
	},
	[USB_PD_PORT_PS8751] = {
		.i2c_port = I2C_PORT_TCPC1,
		.i2c_addr = NX20P3483_ADDR2,
		.drv = &nx20p3483_drv,
		.flags = PPC_CFG_FLAGS_GPIO_CONTROL,
		.snk_gpio = GPIO_USB_C1_CHARGE_ON,
		.src_gpio = GPIO_EN_USB_C1_5V_OUT,
	},
};
const unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

/******************************************************************************/
/* Power Delivery and charing functions */

void variant_tcpc_init(void)
{
	/* Enable PPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_PD_C0_INT_L);
	gpio_enable_interrupt(GPIO_USB_PD_C1_INT_L);

	/* Enable TCPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_PD_INT_ODL);
}
/* Called after the baseboard_tcpc_init (via +2) */
DECLARE_HOOK(HOOK_INIT, variant_tcpc_init, HOOK_PRIO_INIT_I2C + 2);

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_USB_C0_PD_INT_ODL))
		status |= PD_STATUS_TCPC_ALERT_0;

	if (!gpio_get_level(GPIO_USB_C1_PD_INT_ODL)) {
		if (gpio_get_level(GPIO_USB_C1_PD_RST_ODL))
			status |= PD_STATUS_TCPC_ALERT_1;
	}

	return status;
}

/**
 * Reset all system PD/TCPC MCUs -- currently only called from
 * handle_pending_reboot() in common/power.c just before hard
 * resetting the system. This logic is likely not needed as the
 * PP3300_A rail should be dropped on EC reset.
 */
void board_reset_pd_mcu(void)
{
	/* C0: ANX7447 does not have a reset pin. */

	/* C1: Assert reset to TCPC1 (PS8751) for required delay (1ms) */
	gpio_set_level(GPIO_USB_C1_PD_RST_ODL, 0);
	msleep(PS8XXX_RESET_DELAY_MS);
	gpio_set_level(GPIO_USB_C1_PD_RST_ODL, 1);
}
