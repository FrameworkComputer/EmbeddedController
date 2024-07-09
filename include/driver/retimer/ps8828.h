/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Parade 8828 USB/DP Mux.
 */

#ifndef __CROS_EC_USB_MUX_PARADE8828_H
#define __CROS_EC_USB_MUX_PARADE8828_H

#include "usb_mux.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PS8828_I2C_ADDR0_FLAG 0x10
#define PS8828_I2C_ADDR1_FLAG 0x30
#define PS8828_I2C_ADDR2_FLAG 0x50
#define PS8828_I2C_ADDR3_FLAG 0x90

/* Page 0 registers */
#define PS8828_REG_PAGE0 0x00

/* Mode register */
#define PS8828_REG_MODE 0x75
#define PS8828_MODE_ALT_DP_EN BIT(7)
#define PS8828_MODE_USB_EN BIT(6)
#define PS8828_MODE_FLIP BIT(5)

/* DP HPD register */
#define PS8828_REG_DPHPD 0x76
#define PS8828_DPHPD_INHPD_DISABLE BIT(7)
#define PS8828_DPHPD_PLUGGED BIT(6)

extern const struct usb_mux_driver ps8828_usb_retimer_driver;

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_USB_MUX_PARADE8828_H */
