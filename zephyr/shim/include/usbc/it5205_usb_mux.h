/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_SHIM_IT5205_USB_MUX_H
#define __ZEPHYR_SHIM_IT5205_USB_MUX_H

#include "usb_mux/it5205_public.h"

#define IT5205_USB_MUX_COMPAT	ite_it5205

#define USB_MUX_CONFIG_IT5205(mux_id, port_id, idx)			\
	{								\
		USB_MUX_COMMON_FIELDS(mux_id, port_id, idx),		\
		.driver = &it5205_usb_mux_driver,			\
		.i2c_port = I2C_PORT(DT_PHANDLE(mux_id, port)),		\
		.i2c_addr_flags =					\
			DT_STRING_UPPER_TOKEN(mux_id, i2c_addr_flags),	\
	}

#endif /* __ZEPHYR_SHIM_IT5205_USB_MUX_H */
