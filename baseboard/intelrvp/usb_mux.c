/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel BASEBOARD-RVP USB MUX specific configuration */

#include "common.h"
#include "anx7440.h"
#include "bb_retimer.h"
#include "timer.h"
#include "usb_mux.h"

#ifdef CONFIG_USBC_RETIMER_INTEL_BB
struct usb_mux usbc0_retimer = {
	.usb_port = TYPE_C_PORT_0,
	.driver = &bb_usb_retimer,
	.i2c_port = I2C_PORT0_BB_RETIMER,
	.i2c_addr_flags = I2C_PORT0_BB_RETIMER_ADDR,
};
#ifdef HAS_TASK_PD_C1
struct usb_mux usbc1_retimer = {
	.usb_port = TYPE_C_PORT_1,
	.driver = &bb_usb_retimer,
	.i2c_port = I2C_PORT1_BB_RETIMER,
	.i2c_addr_flags = I2C_PORT1_BB_RETIMER_ADDR,
};
#endif /* HAS_TASK_PD_C1 */
#endif

/* USB muxes Configuration */
#ifdef CONFIG_USB_MUX_VIRTUAL
const struct usb_mux usb_muxes[] = {
	[TYPE_C_PORT_0] = {
		.usb_port = TYPE_C_PORT_0,
		.driver = &virtual_usb_mux_driver,
		.hpd_update = &virtual_hpd_update,
#ifdef CONFIG_USBC_RETIMER_INTEL_BB
		.next_mux = &usbc0_retimer,
#endif
	},
#ifdef HAS_TASK_PD_C1
	[TYPE_C_PORT_1] = {
		.usb_port = TYPE_C_PORT_1,
		.driver = &virtual_usb_mux_driver,
		.hpd_update = &virtual_hpd_update,
#ifdef CONFIG_USBC_RETIMER_INTEL_BB
		.next_mux = &usbc1_retimer,
#endif
	},
#endif /* HAS_TASK_PD_C1 */
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == CONFIG_USB_PD_PORT_MAX_COUNT);
#endif /* CONFIG_USB_MUX_VIRTUAL */

#ifdef CONFIG_USB_MUX_ANX7440
const struct usb_mux usb_muxes[] = {
	[TYPE_C_PORT_0] = {
		.usb_port = TYPE_C_PORT_0,
		.i2c_port = I2C_PORT_USB_MUX,
		.i2c_addr_flags = I2C_ADDR_USB_MUX0_FLAGS,
		.driver = &anx7440_usb_mux_driver,
#ifdef CONFIG_USBC_RETIMER_INTEL_BB
		.next_mux = &usbc0_retimer,
#endif
	},
#ifdef HAS_TASK_PD_C1
	[TYPE_C_PORT_1] = {
		.usb_port = TYPE_C_PORT_1,
		.i2c_port = I2C_PORT_USB_MUX,
		.i2c_addr_flags = I2C_ADDR_USB_MUX1_FLAGS,
		.driver = &anx7440_usb_mux_driver,
#ifdef CONFIG_USBC_RETIMER_INTEL_BB
		.next_mux = &usbc1_retimer,
#endif
	},
#endif /* HAS_TASK_PD_C1 */
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == CONFIG_USB_PD_PORT_MAX_COUNT);
#endif /* CONFIG_USB_MUX_ANX7440 */
