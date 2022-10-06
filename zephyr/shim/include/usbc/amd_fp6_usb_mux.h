/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_SHIM_AMD_FP6_USB_MUX_H
#define __ZEPHYR_SHIM_AMD_FP6_USB_MUX_H

#include "usb_mux.h"

#define AMD_FP6_USB_MUX_COMPAT amd_usbc_mux_amd_fp6

#define USB_MUX_CONFIG_AMD_FP6(mux_id)                         \
	{                                                      \
		USB_MUX_COMMON_FIELDS(mux_id),                 \
			.driver = &amd_fp6_usb_mux_driver,     \
			.i2c_port = I2C_PORT_BY_DEV(mux_id),   \
			.i2c_addr_flags = DT_REG_ADDR(mux_id), \
	}

#endif /* __ZEPHYR_SHIM_AMD_FP6_USB_MUX_H */
