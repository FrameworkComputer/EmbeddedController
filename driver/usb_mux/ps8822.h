/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Parade PS8822
 * USB Type-C Retiming Switch for USB Device / DisplayPort Sink
 */

#ifndef __CROS_EC_PS8822_H
#define __CROS_EC_PS8822_H

#include "usb_mux.h"

#define PS8822_I2C_ADDR0_FLAG    0x10
#define PS8822_I2C_ADDR1_FLAG    0x18
#define PS8822_I2C_ADDR2_FLAG    0x58
#define PS8822_I2C_ADDR3_FLAG    0x60

/* Mode register for setting mux */
#define PS8822_REG_MODE         0x01
#define PS8822_MODE_ALT_DP_EN   BIT(7)
#define PS8822_MODE_USB_EN      BIT(6)
#define PS8822_MODE_FLIP        BIT(5)
#define PS8822_MODE_PIN_E       BIT(4)

#define PS8822_REG_CONFIG       0x02
#define PS8822_CONFIG_HPD_IN_DIS BIT(7)
#define PS8822_CONFIG_DP_PLUG    BIT(6)

#define PS8822_REG_DEV_ID1      0x06
#define PS8822_REG_DEV_ID2      0x07
#define PS8822_REG_DEV_ID3      0x08
#define PS8822_REG_DEV_ID4      0x09
#define PS8822_REG_DEV_ID5      0x0A
#define PS8822_REG_DEV_ID6      0x0B

#define PS8822_ID_LEN 6

#endif /* __CROS_EC_PS8822_H */
