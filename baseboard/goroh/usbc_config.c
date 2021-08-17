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

__overridable int board_c1_ps8818_mux_init(const struct usb_mux *me)
{
	/* enable C1 mux power */
	GPIO_SET_LEVEL(GPIO_EN_USB_C1_MUX_PWR, 1);
	return 0;
}

__overridable int board_c1_ps8818_mux_set(const struct usb_mux *me,
					  mux_state_t mux_state)
{
	if (mux_state == USB_PD_MUX_NONE)
		GPIO_SET_LEVEL(GPIO_EN_USB_C1_MUX_PWR, 0);

	return 0;
}

const struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		/* C0 no mux */
	},
	{
		.usb_port = USBC_PORT_C1,
		.i2c_port = I2C_PORT_USB_C1,
		.i2c_addr_flags = PS8818_I2C_ADDR_FLAGS,
		.driver = &ps8818_usb_retimer_driver,
		.board_init = &board_c1_ps8818_mux_init,
		.board_set = &board_c1_ps8818_mux_set,
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
