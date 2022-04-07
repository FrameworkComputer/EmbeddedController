/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_SHIM_TUSB1064_USB_MUX_H
#define __ZEPHYR_SHIM_TUSB1064_USB_MUX_H

#include "driver/usb_mux/tusb1064.h"

#define TUSB1064_USB_MUX_COMPAT	ti_tusb1064

#if defined(CONFIG_USB_MUX_TUSB1044)
#define USB_MUX_CONFIG_TUSB1064(mux_id, port_id, idx)			\
	{								\
		USB_MUX_COMMON_FIELDS(mux_id, port_id, idx),		\
		.driver = &tusb1064_usb_mux_driver,			\
		.i2c_port = I2C_PORT(DT_PHANDLE(mux_id, port)),		\
		.i2c_addr_flags =					\
			DT_STRING_UPPER_TOKEN(mux_id, i2c_addr_flags),	\
		.hpd_update = &tusb1044_hpd_update,			\
	}
#else
#define USB_MUX_CONFIG_TUSB1064(mux_id, port_id, idx)			\
	{								\
		USB_MUX_COMMON_FIELDS(mux_id, port_id, idx),		\
		.driver = &tusb1064_usb_mux_driver,			\
		.i2c_port = I2C_PORT(DT_PHANDLE(mux_id, port)),		\
		.i2c_addr_flags =					\
			DT_STRING_UPPER_TOKEN(mux_id, i2c_addr_flags),	\
	}
#endif /* defined(CONFIG_USB_MUX_TUSB1044) */

#endif /* __ZEPHYR_SHIM_TUBS1064_USB_MUX_H */
