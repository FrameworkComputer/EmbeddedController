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

#define PS8822_REG_PAGE0	0x00

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

#define PS8822_REG_PAGE1        0x01
#define PS8822_REG_DP_EQ        0xB6
#define PS8822_DP_EQ_AUTO_EN    BIT(7)

#define PS8822_DPEQ_LEVEL_UP_9DB    0x00
#define PS8822_DPEQ_LEVEL_UP_11DB   0x01
#define PS8822_DPEQ_LEVEL_UP_12DB   0x02
#define PS8822_DPEQ_LEVEL_UP_14DB   0x03
#define PS8822_DPEQ_LEVEL_UP_17DB   0x04
#define PS8822_DPEQ_LEVEL_UP_18DB   0x05
#define PS8822_DPEQ_LEVEL_UP_19DB   0x06
#define PS8822_DPEQ_LEVEL_UP_20DB   0x07
#define PS8822_DPEQ_LEVEL_UP_21DB   0x08
#define PS8822_DPEQ_LEVEL_UP_MASK   0x0F
#define PS8822_REG_DP_EQ_SHIFT      3

/**
 * Set DP Rx Equalization value
 *
 * @param *me pointer to usb_mux descriptor
 * @param db requested gain setting for DP Rx path
 * @return EC_SUCCESS if db param is valid and I2C is successful
 */
int ps8822_set_dp_rx_eq(const struct usb_mux *me, int db);

#endif /* __CROS_EC_PS8822_H */
