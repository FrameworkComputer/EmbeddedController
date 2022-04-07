/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_SHIM_VIRTUAL_USB_MUX_H
#define __ZEPHYR_SHIM_VIRTUAL_USB_MUX_H

#include "usb_mux.h"

#define VIRTUAL_USB_MUX_COMPAT	cros_ec_usbc_mux_virtual

#define USB_MUX_CONFIG_VIRTUAL(mux_id, port_id, idx)			\
	{								\
		USB_MUX_COMMON_FIELDS(mux_id, port_id, idx),		\
		.driver = &virtual_usb_mux_driver,			\
		.hpd_update = &virtual_hpd_update,			\
	}

#endif /* __ZEPHYR_SHIM_VIRTUAL_USB_MUX_H */
