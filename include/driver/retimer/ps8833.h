/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Parade 8828 USB/DP Mux.
 */

#ifndef __CROS_EC_USB_MUX_PARADE8833_H
#define __CROS_EC_USB_MUX_PARADE8833_H

#include "usb_mux.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PS8833_ADDR0 0x10
#define PS8833_ADDR1 0x20
#define PS8833_ADDR3 0x30
#define PS8833_ADDR4 0x40
#define PS8833_ADDR5 0x50
#define PS8833_ADDR6 0x60
#define PS8833_ADDR7 0x70
#define PS8833_ADDR8 0x80

#define PS8833_REG_PAGE0 0x00

/* This register contains a mix of general bits a*/
#define PS8833_REG_MODE 0x00
#define PS8833_REG_MODE_USB_EN BIT(5)
#define PS8833_REG_MODE_FLIP BIT(1)
/* Used for both connected and safe-states. */
#define PS8833_REG_MODE_CONN BIT(0)

#define PS8833_REG_DP 0x01
#define PS8833_REG_DP_EN BIT(0)
#define PS8833_REG_DP_SINK BIT(1)
#define PS8833_REG_DP_PIN_SHIFT 2
#define PS8833_REG_DP_PIN_MASK GENMASK(3, 2)
#define PS8833_REG_DP_PIN_E (0x00 << PS8833_REG_DP_PIN_SHIFT)
#define PS8833_REG_DP_PIN_CD (0x1 << PS8833_REG_DP_PIN_SHIFT)
#define PS8833_REG_DP_HPD BIT(7)

#define PS8833_REG_TBT3_USB4 0x02
#define PS8833_REG_TBT3_USB4_TBT3_EN BIT(0)
#define PS8833_REG_TBT3_USB4_USB4_EN BIT(7)

extern const struct usb_mux_driver ps8833_usb_retimer_driver;

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_USB_MUX_PARADE8833_H */
