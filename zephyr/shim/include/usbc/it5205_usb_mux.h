/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_SHIM_IT5205_USB_MUX_H
#define __ZEPHYR_SHIM_IT5205_USB_MUX_H

#include "usb_mux/it5205_public.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IT5205_USB_MUX_COMPAT ite_it5205

/* clang-format off */
#define USB_MUX_CONFIG_IT5205(mux_id)                  \
	{                                              \
		USB_MUX_COMMON_FIELDS(mux_id),         \
		.driver = &it5205_usb_mux_driver,      \
		.i2c_port = I2C_PORT_BY_DEV(mux_id),   \
		.i2c_addr_flags = DT_REG_ADDR(mux_id), \
	}
/* clang-format on */

#ifdef __cplusplus
}
#endif

#endif /* __ZEPHYR_SHIM_IT5205_USB_MUX_H */
