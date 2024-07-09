/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Analogix Type-C port controller */

#ifndef __CROS_EC_DRIVER_TCPM_ANX7447_PUBLIC_H
#define __CROS_EC_DRIVER_TCPM_ANX7447_PUBLIC_H

#include "usb_mux.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AN7447_TCPC0_I2C_ADDR_FLAGS 0x2C
#define AN7447_TCPC1_I2C_ADDR_FLAGS 0x2B
#define AN7447_TCPC2_I2C_ADDR_FLAGS 0x2A
#define AN7447_TCPC3_I2C_ADDR_FLAGS 0x29

#define AN7447_SPI0_I2C_ADDR_FLAGS 0x3F
#define AN7447_SPI1_I2C_ADDR_FLAGS 0x37
#define AN7447_SPI2_I2C_ADDR_FLAGS 0x32
#define AN7447_SPI3_I2C_ADDR_FLAGS 0x31

extern const struct tcpm_drv anx7447_tcpm_drv;
extern const struct usb_mux_driver anx7447_usb_mux_driver;

void anx7447_tcpc_update_hpd_status(const struct usb_mux *me,
				    mux_state_t mux_state, bool *ack_required);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_DRIVER_TCPM_ANX7447_PUBLIC_H */
