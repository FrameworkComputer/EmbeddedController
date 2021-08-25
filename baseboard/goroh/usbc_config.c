/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Goroh family-specific USB-C configuration */
#include <stdint.h>
#include <stdbool.h>

#include "common.h"
#include "compile_time_macros.h"
#include "config.h"
#include "console.h"
#include "hooks.h"
#include "driver/tcpm/it8xxx2_pd_public.h"
#include "driver/ppc/syv682x_public.h"
#include "driver/retimer/ps8818.h"
#include "driver/tcpm/tcpci.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "gpio.h"
#include "gpio_signal.h"

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

#ifdef CONFIG_BRINGUP
#define GPIO_SET_LEVEL(pin, lvl) gpio_set_level_verbose(CC_USBPD, pin, lvl)
#else
#define GPIO_SET_LEVEL(pin, lvl) gpio_set_level(pin, lvl)
#endif

/* PPC */
struct ppc_config_t ppc_chips[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr_flags = SYV682X_ADDR0_FLAGS,
		.drv = &syv682x_drv,
	},
	{
		.i2c_port = I2C_PORT_USB_C1,
		.i2c_addr_flags = SYV682X_ADDR0_FLAGS,
		.drv = &syv682x_drv,
	},
};
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

/* USB Mux */
static int goroh_usb_c0_init_mux(const struct usb_mux *me)
{
	return virtual_usb_mux_driver.init(me);
}

static int goroh_usb_c0_set_mux(const struct usb_mux *me, mux_state_t mux_state,
				bool *ack_required)
{
	/*
	 * b/188376636: Inverse C0 polarity.
	 * Goroh rev0 CC1/CC2 SBU1/SBU2 are reversed.
	 * We report inversed polarity to the SoC and SoC we reverse the SBU
	 * accordingly.
	 */
	mux_state = mux_state ^ USB_PD_MUX_POLARITY_INVERTED;

	return virtual_usb_mux_driver.set(me, mux_state, ack_required);

}

static int goroh_usb_c0_get_mux(const struct usb_mux *me,
				mux_state_t *mux_state)
{
	return virtual_usb_mux_driver.get(me, mux_state);
}

static struct usb_mux_driver goroh_usb_c0_mux_driver = {
	.init = goroh_usb_c0_init_mux,
	.set = goroh_usb_c0_set_mux,
	.get = goroh_usb_c0_get_mux,
};

static const struct usb_mux goroh_usb_c1_ps8818_retimer = {
	.usb_port = USBC_PORT_C1,
	.i2c_port = I2C_PORT_USB_C1,
	.i2c_addr_flags = PS8818_I2C_ADDR_FLAGS,
	.driver = &ps8818_usb_retimer_driver,
	.next_mux = NULL,
};

const struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USBC_PORT_C0] = {
		.usb_port = USBC_PORT_C0,
		.driver = &goroh_usb_c0_mux_driver,
		.hpd_update = &virtual_hpd_update,
	},
	[USBC_PORT_C1] = {
		.usb_port = USBC_PORT_C1,
		.driver = &virtual_usb_mux_driver,
		.hpd_update = &virtual_hpd_update,
		.next_mux = &goroh_usb_c1_ps8818_retimer,
	},
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == USBC_PORT_COUNT);

/* TCPC */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_EMBEDDED,
		/* TCPC is embedded within EC so no i2c config needed */
		.drv = &it8xxx2_tcpm_drv,
		/* Alert is active-low, push-pull */
		.flags = 0,
	},
	{
		.bus_type = EC_BUS_TYPE_EMBEDDED,
		/* TCPC is embedded within EC so no i2c config needed */
		.drv = &it8xxx2_tcpm_drv,
		/* Alert is active-low, push-pull */
		.flags = 0,
	},
};

void ppc_interrupt(enum gpio_signal signal)
{
	if (signal == GPIO_USB_C0_FAULT_ODL)
		/* C0: PPC interrupt */
		syv682x_interrupt(0);
	else
		/* C1: PPC interrupt */
		syv682x_interrupt(1);
}


static void board_tcpc_init(void)
{
	gpio_enable_interrupt(GPIO_USB_C0_FAULT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_FAULT_ODL);
}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_I2C + 1);

void board_reset_pd_mcu(void)
{
	/*
	 * C0 & C1: TCPC is embedded in the EC and processes interrupts in the
	 * chip code (it83xx/intc.c)
	 */
}

uint16_t tcpc_get_alert_status(void)
{
	/*
	 * C0 & C1: TCPC is embedded in the EC and processes interrupts in the
	 * chip code (it83xx/intc.c)
	 */
	return 0;
}
