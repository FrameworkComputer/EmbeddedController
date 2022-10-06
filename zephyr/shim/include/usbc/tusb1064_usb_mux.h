/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_SHIM_TUSB1064_USB_MUX_H
#define __ZEPHYR_SHIM_TUSB1064_USB_MUX_H

#include "driver/usb_mux/tusb1064.h"

#define TUSB1064_USB_MUX_COMPAT ti_tusb1064

#if defined(CONFIG_USB_MUX_TUSB1044)
#define USB_MUX_CONFIG_TUSB1064(mux_id)                        \
	{                                                      \
		USB_MUX_COMMON_FIELDS(mux_id),                 \
			.driver = &tusb1064_usb_mux_driver,    \
			.i2c_port = I2C_PORT_BY_DEV(mux_id),   \
			.i2c_addr_flags = DT_REG_ADDR(mux_id), \
			.hpd_update = &tusb1044_hpd_update,    \
	}
#elif defined(CONFIG_USB_MUX_TUSB546)
#define USB_MUX_CONFIG_TUSB1064(mux_id)                        \
	{                                                      \
		USB_MUX_COMMON_FIELDS(mux_id),                 \
			.driver = &tusb1064_usb_mux_driver,    \
			.i2c_port = I2C_PORT_BY_DEV(mux_id),   \
			.i2c_addr_flags = DT_REG_ADDR(mux_id), \
	}
#else
#define USB_MUX_CONFIG_TUSB1064(mux_id)                        \
	{                                                      \
		USB_MUX_COMMON_FIELDS(mux_id),                 \
			.driver = &tusb1064_usb_mux_driver,    \
			.i2c_port = I2C_PORT_BY_DEV(mux_id),   \
			.i2c_addr_flags = DT_REG_ADDR(mux_id), \
	}
#endif /* defined(CONFIG_USB_MUX_TUSB1044) */

#endif /* __ZEPHYR_SHIM_TUBS1064_USB_MUX_H */
