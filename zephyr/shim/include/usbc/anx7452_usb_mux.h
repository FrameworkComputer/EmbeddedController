/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_SHIM_ANX7452_USB_MUX_H
#define __ZEPHYR_SHIM_ANX7452_USB_MUX_H

#include "driver/retimer/anx7452_public.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ANX7452_USB_MUX_COMPAT analogix_anx7452

#define ANX7452_USB_EN_GPIO(mux_id) GPIO_SIGNAL(DT_PHANDLE(mux_id, usb_en_pin))

#define ANX7452_DP_EN_GPIO(mux_id)                                \
	COND_CODE_1(DT_NODE_HAS_PROP(mux_id, dp_en_pin),          \
		    (GPIO_SIGNAL(DT_PHANDLE(mux_id, dp_en_pin))), \
		    (GPIO_UNIMPLEMENTED))

#define ANX7452_CONTROLS_CONFIG(mux_id)                         \
	{                                                       \
		.usb_enable_gpio = ANX7452_USB_EN_GPIO(mux_id), \
		.dp_enable_gpio = ANX7452_DP_EN_GPIO(mux_id),   \
	}

#define USB_MUX_ANX7452_CONTROL_ARRAY(mux_id) \
	[USB_MUX_PORT(mux_id)] = ANX7452_CONTROLS_CONFIG(mux_id),

#define USB_MUX_ANX7452_CONTROLS_ARRAY                 \
	DT_FOREACH_STATUS_OKAY(ANX7452_USB_MUX_COMPAT, \
			       USB_MUX_ANX7452_CONTROL_ARRAY)

#define USB_MUX_CONFIG_ANX7452(mux_id)                         \
	{                                                      \
		USB_MUX_COMMON_FIELDS(mux_id),                 \
			.driver = &anx7452_usb_retimer_driver, \
			.i2c_port = I2C_PORT_BY_DEV(mux_id),   \
			.i2c_addr_flags = DT_REG_ADDR(mux_id), \
	}

#ifdef __cplusplus
}
#endif

#endif /* __ZEPHYR_SHIM_ANX7452_USB_MUX_H */
