/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_SHIM_BB_RETIMER_USB_MUX_H
#define __ZEPHYR_SHIM_BB_RETIMER_USB_MUX_H

#include "driver/retimer/bb_retimer_public.h"

#define BB_RETIMER_USB_MUX_COMPAT intel_jhl8040r

#define USB_MUX_CONFIG_BB_RETIMER(mux_id, port_id, idx)                    \
	{                                                                  \
		USB_MUX_COMMON_FIELDS(mux_id, port_id, idx),               \
			.driver = &bb_usb_retimer,                         \
			.hpd_update = bb_retimer_hpd_update,               \
			.i2c_port = I2C_PORT(DT_PHANDLE(mux_id, port)),    \
			.i2c_addr_flags = DT_PROP(mux_id, i2c_addr_flags), \
	}

#define BB_RETIMER_CONTROLS_CONFIG(mux_id, port_id, idx)              \
	{                                                             \
		.retimer_rst_gpio =                                   \
			GPIO_SIGNAL(DT_PHANDLE(mux_id, reset_pin)),   \
		.usb_ls_en_gpio = COND_CODE_1(                        \
			DT_NODE_HAS_PROP(mux_id, ls_en_pin),          \
			(GPIO_SIGNAL(DT_PHANDLE(mux_id, ls_en_pin))), \
			(GPIO_UNIMPLEMENTED)),                        \
	}

#endif /* __ZEPHYR_SHIM_BB_RETIMER_USB_MUX_H */
