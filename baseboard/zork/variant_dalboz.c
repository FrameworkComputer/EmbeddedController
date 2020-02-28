/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "driver/usb_mux/amd_fp5.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "ioexpander.h"
#include "usb_mux.h"

static void usba_retimer_on(void)
{
	ioex_set_level(IOEX_USB_A1_RETIMER_EN, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, usba_retimer_on, HOOK_PRIO_DEFAULT);

static void usba_retimer_off(void)
{
	ioex_set_level(IOEX_USB_A1_RETIMER_EN, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, usba_retimer_off, HOOK_PRIO_DEFAULT);

const struct usb_mux usb_muxes[] = {
	[USBC_PORT_C0] = {
		.usb_port = USBC_PORT_C0,
		.i2c_port = I2C_PORT_USB_AP_MUX,
		.i2c_addr_flags = AMD_FP5_MUX_I2C_ADDR_FLAGS,
		.driver = &amd_fp5_usb_mux_driver,
	},
	[USBC_PORT_C1] = {
		.usb_port = USBC_PORT_C1,
		.i2c_port = I2C_PORT_TCPC1,
		.i2c_addr_flags = 0x10,
		.driver = &ps874x_usb_mux_driver,
	}
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == USBC_PORT_COUNT);
