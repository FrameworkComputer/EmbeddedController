/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common code for VARIANT_OCTOPUS_USBC_EC_TCPCS configuration */

#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "driver/ppc/sn5s330.h"
#include "driver/tcpm/it83xx_pd.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/tcpci.h"
#include "driver/tcpm/tcpm.h"
#include "driver/usb_mux/it5205.h"
#include "gpio.h"
#include "hooks.h"
#include "system.h"
#include "tcpm/tcpci.h"
#include "usb_mux.h"
#include "usbc_ppc.h"
#include "util.h"

#define USB_PD_PORT_ITE_0 0
#define USB_PD_PORT_ITE_1 1

/******************************************************************************/
/* USB-C TPCP Configuration */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_ITE_0] = {
		.bus_type = EC_BUS_TYPE_EMBEDDED,
		/* TCPC is embedded within EC so no i2c config needed */
		.drv = &it83xx_tcpm_drv,
		/* Alert is active-low, push-pull */
		.flags = 0,
	},
	[USB_PD_PORT_ITE_1] = {
		.bus_type = EC_BUS_TYPE_EMBEDDED,
		/* TCPC is embedded within EC so no i2c config needed */
		.drv = &it83xx_tcpm_drv,
		/* Alert is active-low, push-pull */
		.flags = 0,
	},
};

/******************************************************************************/
/* USB-C MUX Configuration */

/* TODO(crbug.com/826441): Consolidate this logic with other impls */
static void board_it83xx_hpd_status(const struct usb_mux *me,
				    mux_state_t mux_state, bool *ack_required)
{
	int hpd_lvl = (mux_state & USB_PD_MUX_HPD_LVL) ? 1 : 0;
	int hpd_irq = (mux_state & USB_PD_MUX_HPD_IRQ) ? 1 : 0;
	enum gpio_signal gpio = me->usb_port ? GPIO_USB_C1_HPD_1V8_ODL :
					       GPIO_USB_C0_HPD_1V8_ODL;

	/* This driver does not use host command ACKs */
	*ack_required = false;

	/* Invert HPD level since GPIOs are active low. */
	hpd_lvl = !hpd_lvl;

	gpio_set_level(gpio, hpd_lvl);
	if (hpd_irq) {
		gpio_set_level(gpio, 1);
		crec_msleep(1);
		gpio_set_level(gpio, hpd_lvl);
	}
}

/* This configuration might be override by each boards */
struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_ITE_0] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USB_PD_PORT_ITE_0,
			.i2c_port = I2C_PORT_USB_MUX,
			.i2c_addr_flags = IT5205_I2C_ADDR1_FLAGS,
			.driver = &it5205_usb_mux_driver,
			.hpd_update = &board_it83xx_hpd_status,
		},
	},
	[USB_PD_PORT_ITE_1] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USB_PD_PORT_ITE_1,
			/* Use PS8751 as mux only */
			.i2c_port = I2C_PORT_USBC1,
			.i2c_addr_flags = PS8XXX_I2C_ADDR1_FLAGS,
			.flags = USB_MUX_FLAG_NOT_TCPC,
			.driver = &ps8xxx_usb_mux_driver,
			.hpd_update = &ps8xxx_tcpc_update_hpd_status,
		},
	}
};

/******************************************************************************/
/* USB-C PPC Configuration */
struct ppc_config_t ppc_chips[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_ITE_0] = { .i2c_port = I2C_PORT_USBC0,
				.i2c_addr_flags = SN5S330_ADDR0_FLAGS,
				.drv = &sn5s330_drv },
	[USB_PD_PORT_ITE_1] = { .i2c_port = I2C_PORT_USBC1,
				.i2c_addr_flags = SN5S330_ADDR0_FLAGS,
				.drv = &sn5s330_drv },
};
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

/******************************************************************************/
/* Power Delivery and charing functions */

void variant_tcpc_init(void)
{
	/* Enable PPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_PD_INT_ODL);
}
/* Called after the baseboard_tcpc_init (via +3) */
DECLARE_HOOK(HOOK_INIT, variant_tcpc_init, HOOK_PRIO_INIT_I2C + 3);

uint16_t tcpc_get_alert_status(void)
{
	/*
	 * Since C0/C1 TCPC are embedded within EC, we don't need the PDCMD
	 * tasks.The (embedded) TCPC status since chip driver code will
	 * handles its own interrupts and forward the correct events to
	 * the PD_C0 task. See it83xx/intc.c
	 */
	return 0;
}

/**
 * Reset all system PD/TCPC MCUs -- currently called from both
 * handle_pending_reboot() in common/system.c and baseboard_tcpc_init() in the
 * octopus/baseboard.c
 */
void board_reset_pd_mcu(void)
{
	/*
	 * C0 & C1: The internal TCPC on ITE EC does not have a reset signal,
	 * but it will get reset when the EC gets reset.  We will, however,
	 * reset the USB muxes here.
	 */
	gpio_set_level(GPIO_USB_C0_PD_RST_ODL, 0);
	gpio_set_level(GPIO_USB_C1_PD_RST_ODL, 0);

	crec_msleep(PS8XXX_RESET_DELAY_MS);

	gpio_set_level(GPIO_USB_C0_PD_RST_ODL, 1);
	gpio_set_level(GPIO_USB_C1_PD_RST_ODL, 1);
}

void board_pd_vconn_ctrl(int port, enum usbpd_cc_pin cc_pin, int enabled)
{
	/*
	 * We ignore the cc_pin because the polarity should already be set
	 * correctly in the PPC driver via the pd state machine.
	 */
	if (ppc_set_vconn(port, enabled) != EC_SUCCESS)
		cprints(CC_USBPD, "C%d: Failed %sabling vconn", port,
			enabled ? "en" : "dis");
}
