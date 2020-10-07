/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel BASEBOARD-RVP Retimer specific configuration */

#include "bb_retimer.h"
#include "compile_time_macros.h"
#include "common.h"

/* USB Retimers configuration */
#ifdef CONFIG_USBC_RETIMER_INTEL_BB
const struct bb_usb_control bb_controls[] = {
	[TYPE_C_PORT_0] = {
		.usb_ls_en_gpio = GPIO_USB_C0_LS_EN,
		.retimer_rst_gpio = GPIO_USB_C0_RETIMER_RST,
	},
#ifdef HAS_TASK_PD_C1
	[TYPE_C_PORT_1] = {
		.usb_ls_en_gpio = GPIO_USB_C1_LS_EN,
		.retimer_rst_gpio = GPIO_USB_C1_RETIMER_RST,
	},
#endif /* HAS_TASK_PD_C1 */
};
BUILD_ASSERT(ARRAY_SIZE(bb_controls) == CONFIG_USB_PD_PORT_MAX_COUNT);

#endif /* CONFIG_USBC_RETIMER_INTEL_BB */
