/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_SHIM_BB_RETIMER_USB_MUX_H
#define __ZEPHYR_SHIM_BB_RETIMER_USB_MUX_H

#include "driver/retimer/bb_retimer_public.h"

#define BB_RETIMER_USB_MUX_COMPAT intel_jhl8040r

#define USB_MUX_CONFIG_BB_RETIMER(mux_id)                      \
	{                                                      \
		USB_MUX_COMMON_FIELDS(mux_id),                 \
			.driver = &bb_usb_retimer,             \
			.hpd_update = bb_retimer_hpd_update,   \
			.i2c_port = I2C_PORT_BY_DEV(mux_id),   \
			.i2c_addr_flags = DT_REG_ADDR(mux_id), \
	}

#define BB_RETIMER_CONTROLS_CONFIG(mux_id)                            \
	{                                                             \
		.retimer_rst_gpio =                                   \
			GPIO_SIGNAL(DT_PHANDLE(mux_id, reset_pin)),   \
		.usb_ls_en_gpio = COND_CODE_1(                        \
			DT_NODE_HAS_PROP(mux_id, ls_en_pin),          \
			(GPIO_SIGNAL(DT_PHANDLE(mux_id, ls_en_pin))), \
			(GPIO_UNIMPLEMENTED)),                        \
	}

/**
 * @brief Set entry in bb_controls array
 *
 * @param mux_id BB retimer node ID
 */
#define USB_MUX_BB_RETIMER_CONTROL_ARRAY(mux_id) \
	[USB_MUX_PORT(mux_id)] = BB_RETIMER_CONTROLS_CONFIG(mux_id),

/**
 * @brief Call USB_MUX_BB_RETIMER_CONTROL_ARRAY for every BB retimer in DTS
 */
#define USB_MUX_BB_RETIMERS_CONTROLS_ARRAY                \
	DT_FOREACH_STATUS_OKAY(BB_RETIMER_USB_MUX_COMPAT, \
			       USB_MUX_BB_RETIMER_CONTROL_ARRAY)

#endif /* __ZEPHYR_SHIM_BB_RETIMER_USB_MUX_H */
