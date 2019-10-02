/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel BASEBOARD-RVP Retimer specific configuration */

#include "bb_retimer.h"
#include "compile_time_macros.h"
#include "common.h"

/* USB Retimers configuration */
#ifdef CONFIG_USB_PD_RETIMER_INTEL_BB
struct bb_retimer bb_retimers[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[TYPE_C_PORT_0] = {
		.i2c_port = I2C_PORT0_BB_RETIMER,
		.i2c_addr = I2C_PORT0_BB_RETIMER_ADDR,
		.shared_nvm = USB_PORT0_BB_RETIMER_SHARED_NVM,
		.usb_ls_en_gpio = GPIO_USB_C0_LS_EN,
		.retimer_rst_gpio = GPIO_USB_C0_RETIMER_RST,
		.force_power_gpio = GPIO_USB_C0_RETIMER_FORCE_PWR,
	},
#ifdef HAS_TASK_PD_C1
	[TYPE_C_PORT_1] = {
		.i2c_port = I2C_PORT1_BB_RETIMER,
		.i2c_addr = I2C_PORT1_BB_RETIMER_ADDR,
		.shared_nvm = USB_PORT1_BB_RETIMER_SHARED_NVM,
		.usb_ls_en_gpio = GPIO_USB_C1_LS_EN,
		.retimer_rst_gpio = GPIO_USB_C1_RETIMER_RST,
		.force_power_gpio = GPIO_USB_C1_RETIMER_FORCE_PWR,
	},
#endif /* HAS_TASK_PD_C1 */
};
BUILD_ASSERT(ARRAY_SIZE(bb_retimers) == CONFIG_USB_PD_PORT_MAX_COUNT);
#endif /* CONFIG_USB_PD_RETIMER_INTEL_BB */
