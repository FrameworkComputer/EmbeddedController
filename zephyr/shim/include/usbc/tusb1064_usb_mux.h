/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_SHIM_TUSB1064_USB_MUX_H
#define __ZEPHYR_SHIM_TUSB1064_USB_MUX_H

#include "driver/usb_mux/tusb1064.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TUSB1064_EMUL_COMPAT zephyr_tusb1064_emul

#if defined(CONFIG_USB_MUX_TUSB1044)
#define TUSB1064_USB_MUX_COMPAT ti_tusb1044
/* clang-format off */
#define USB_MUX_CONFIG_TUSB1064(mux_id)                \
	{                                              \
		USB_MUX_COMMON_FIELDS(mux_id),         \
		.driver = &tusb1064_usb_mux_driver,    \
		.i2c_port = I2C_PORT_BY_DEV(mux_id),   \
		.i2c_addr_flags = DT_REG_ADDR(mux_id), \
		.hpd_update = &tusb1044_hpd_update,    \
	}
#elif defined(CONFIG_USB_MUX_TUSB546)
#define TUSB1064_USB_MUX_COMPAT ti_tusb546
#define USB_MUX_CONFIG_TUSB1064(mux_id)                \
	{                                              \
		USB_MUX_COMMON_FIELDS(mux_id),         \
		.driver = &tusb1064_usb_mux_driver,    \
		.i2c_port = I2C_PORT_BY_DEV(mux_id),   \
		.i2c_addr_flags = DT_REG_ADDR(mux_id), \
	}
#else
#define TUSB1064_USB_MUX_COMPAT ti_tusb1064
#define USB_MUX_CONFIG_TUSB1064(mux_id)                \
	{                                              \
		USB_MUX_COMMON_FIELDS(mux_id),         \
		.driver = &tusb1064_usb_mux_driver,    \
		.i2c_port = I2C_PORT_BY_DEV(mux_id),   \
		.i2c_addr_flags = DT_REG_ADDR(mux_id), \
	}
/* clang-format on */
#endif /* defined(CONFIG_USB_MUX_TUSB1044) */

#ifdef __cplusplus
}
#endif

#endif /* __ZEPHYR_SHIM_TUBS1064_USB_MUX_H */
