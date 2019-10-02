/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel BASEBOARD-RVP USB MUX specific configuration */

#include "common.h"
#include "usb_mux.h"

/* USB muxes Configuration */
#ifdef CONFIG_USB_MUX_VIRTUAL
struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[TYPE_C_PORT_0] = {
		.driver = &virtual_usb_mux_driver,
		.hpd_update = &virtual_hpd_update,
	},
#ifdef HAS_TASK_PD_C1
	[TYPE_C_PORT_1] = {
		.driver = &virtual_usb_mux_driver,
		.hpd_update = &virtual_hpd_update,
	},
#endif /* HAS_TASK_PD_C1 */
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == CONFIG_USB_PD_PORT_MAX_COUNT);
#endif /* CONFIG_USB_MUX_VIRTUAL */
