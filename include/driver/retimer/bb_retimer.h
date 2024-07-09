/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Driver header for Intel Burnside Bridge - Thunderbolt/USB/DisplayPort Retimer
 */

#ifndef __CROS_EC_BB_RETIMER_H
#define __CROS_EC_BB_RETIMER_H

#include "driver/retimer/bb_retimer_public.h"
#include "usb_mux.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Burnside Bridge I2C Configuration Space */
#define BB_RETIMER_REG_VENDOR_ID 0
#define BB_RETIMER_VENDOR_ID_1 0x8086
#define BB_RETIMER_VENDOR_ID_2 0x8087

#define BB_RETIMER_REG_DEVICE_ID 1
#ifdef CONFIG_USBC_RETIMER_INTEL_HB
/* HB has no Device ID field instead it is combined with Vendor ID */
#define BB_RETIMER_DEVICE_ID 0x0D9C8087
#else
#define BB_RETIMER_DEVICE_ID 0x15EE
#endif

/* Connection State Register Attributes */
#define BB_RETIMER_REG_CONNECTION_STATE 4
#define BB_RETIMER_DATA_CONNECTION_PRESENT BIT(0)
#define BB_RETIMER_CONNECTION_ORIENTATION BIT(1)
#define BB_RETIMER_RE_TIMER_DRIVER BIT(2)
#define BB_RETIMER_USB_2_CONNECTION BIT(4)
#define BB_RETIMER_USB_3_CONNECTION BIT(5)
#define BB_RETIMER_USB_3_SPEED BIT(6)
#define BB_RETIMER_USB_DATA_ROLE BIT(7)
#define BB_RETIMER_DP_CONNECTION BIT(8)
#define BB_RETIMER_DP_PIN_ASSIGNMENT BIT(10)
#define BB_RETIMER_IRQ_HPD BIT(14)
#define BB_RETIMER_HPD_LVL BIT(15)
#define BB_RETIMER_TBT_CONNECTION BIT(16)
#define BB_RETIMER_TBT_TYPE BIT(17)
#define BB_RETIMER_TBT_CABLE_TYPE BIT(18)
#define BB_RETIMER_VPRO_DOCK_DP_OVERDRIVE BIT(19)
#define BB_RETIMER_TBT_ACTIVE_LINK_TRAINING BIT(20)
#define BB_RETIMER_ACTIVE_PASSIVE BIT(22)
#define BB_RETIMER_USB4_ENABLED BIT(23)
#define BB_RETIMER_USB4_TBT_CABLE_SPEED_SUPPORT(x) (((x) & 0x7) << 25)
#define BB_RETIMER_TBT_CABLE_GENERATION(x) (((x) & 0x3) << 28)

#define BB_RETIMER_REG_TBT_CONTROL 5
#define BB_RETIMER_REG_EXT_CONNECTION_MODE 7

#define BB_RETIMER_REG_COUNT 8

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_BB_RETIMER_H */
