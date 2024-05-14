/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_SHIM_VIRTUAL_USB_MUX_H
#define __ZEPHYR_SHIM_VIRTUAL_USB_MUX_H

#include "usb_mux.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VIRTUAL_USB_MUX_COMPAT cros_ec_usbc_mux_virtual

#define USB_MUX_CONFIG_VIRTUAL(mux_id)                     \
	{                                                  \
		USB_MUX_COMMON_FIELDS(mux_id),             \
			.driver = &virtual_usb_mux_driver, \
			.hpd_update = &virtual_hpd_update, \
	}

#ifdef __cplusplus
}
#endif

#endif /* __ZEPHYR_SHIM_VIRTUAL_USB_MUX_H */
