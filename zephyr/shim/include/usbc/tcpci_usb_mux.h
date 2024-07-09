/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_SHIM_TCPCI_USB_MUX_H
#define __ZEPHYR_SHIM_TCPCI_USB_MUX_H

#include "dt-bindings/usbc_mux.h"
#include "tcpm/ps8xxx_public.h"
#include "tcpm/tcpci.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TCPCI_TCPM_USB_MUX_COMPAT cros_ec_usbc_mux_tcpci
#define PS8XXX_USB_MUX_COMPAT parade_usbc_mux_ps8xxx

/**
 * Add I2C configuration and USB_MUX_FLAG_NOT_TCPC to enforce it when
 * mux_read()/mux_write() functions are used.
 */
/* clang-format off */
#define USB_MUX_CONFIG_TCPCI_TCPM_WITH_I2C(mux_id)                             \
	{                                                                      \
		USB_MUX_COMMON_FIELDS_WITH_FLAGS(                              \
			mux_id, USB_MUX_FLAG_NOT_TCPC, USB_MUX_FLAG_NOT_TCPC), \
		.driver = &tcpci_tcpm_usb_mux_driver,                          \
		.hpd_update = USB_MUX_CALLBACK_OR_NULL(mux_id, hpd_update),    \
		.i2c_port = I2C_PORT_BY_DEV(mux_id),                           \
		.i2c_addr_flags = DT_REG_ADDR(mux_id),                         \
	}
/* clang-format on */

/** Use I2C configuration from TCPC */
/* clang-format off */
#define USB_MUX_CONFIG_TCPCI_TCPM_WO_I2C(mux_id)                               \
	{                                                                      \
		USB_MUX_COMMON_FIELDS(mux_id),                                 \
		.driver = &tcpci_tcpm_usb_mux_driver,                          \
		.hpd_update = USB_MUX_CALLBACK_OR_NULL(mux_id, hpd_update),    \
	}
/* clang-format on */

#define USB_MUX_CONFIG_TCPCI_TCPM(mux_id)                         \
	COND_CODE_1(DT_NODE_HAS_PROP(mux_id, reg),                \
		    (USB_MUX_CONFIG_TCPCI_TCPM_WITH_I2C(mux_id)), \
		    (USB_MUX_CONFIG_TCPCI_TCPM_WO_I2C(mux_id)))

#ifdef __cplusplus
}
#endif

#endif /* __ZEPHYR_SHIM_TCPCI_USB_MUX_H */
