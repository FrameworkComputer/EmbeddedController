/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_SHIM_PS8828_USB_MUX_H
#define __ZEPHYR_SHIM_PS8828_USB_MUX_H

#include "driver/retimer/ps8828.h"

#define PS8828_USB_MUX_COMPAT parade_ps8828

#define USB_MUX_CONFIG_PS8828(mux_id)                          \
	{                                                      \
		USB_MUX_COMMON_FIELDS(mux_id),                 \
			.driver = &ps8828_usb_retimer_driver,  \
			.i2c_port = I2C_PORT_BY_DEV(mux_id),   \
			.i2c_addr_flags = DT_REG_ADDR(mux_id), \
	}

#endif /* __ZEPHYR_SHIM_PS8828_USB_MUX_H */
