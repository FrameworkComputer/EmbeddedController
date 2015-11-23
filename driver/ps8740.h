/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Parade PS8740 USB port switch driver.
 */

#ifndef __CROS_EC_PS8740_H
#define __CROS_EC_PS8740_H

/* Mode register for setting mux */
#define PS8740_REG_MODE 0x00
#define PS8740_MODE_POLARITY_INVERTED (1 << 4)
#define PS8740_MODE_USB_ENABLED (1 << 5)
#define PS8740_MODE_DP_ENABLED (1 << 6)
#define PS8740_MODE_POWER_DOWN (1 << 7)

/* Status register for checking mux state */
#define PS8740_REG_STATUS 0x09
#define PS8740_STATUS_POLARITY_INVERTED (1 << 2)
#define PS8740_STATUS_USB_ENABLED (1 << 3)
#define PS8740_STATUS_DP_ENABLED (1 << 4)
#define PS8740_STATUS_HPD_ASSERTED (1 << 7)

/* Chip ID / revision registers and expected fused values */
#define PS8740_REG_REVISION_ID1 0xf0
#define PS8740_REVISION_ID1 0x00
#define PS8740_REG_REVISION_ID2 0xf1
#define PS8740_REVISION_ID2 0x0a
#define PS8740_REG_CHIP_ID1 0xf2
#define PS8740_CHIP_ID1 0x40
#define PS8740_REG_CHIP_ID2 0xf3
#define PS8740_CHIP_ID2 0x87

/* USB equalization settings for Host to Mux */
#define PS8740_REG_USB_EQ_TX     0x32
#define PS8740_USB_EQ_TX_10_1_DB 0x00
#define PS8740_USB_EQ_TX_14_3_DB 0x20
#define PS8740_USB_EQ_TX_8_5_DB  0x40
#define PS8740_USB_EQ_TX_6_5_DB  0x60
#define PS8740_USB_EQ_TX_11_5_DB 0x80
#define PS8740_USB_EQ_TX_9_5_DB  0xc0
#define PS8740_USB_EQ_TX_7_5_DB  0xe0
#define PS8740_USB_EQ_TERM_100_OHM (0 << 2)
#define PS8740_USB_EQ_TERM_85_OHM  (1 << 2)

/* USB equalization settings for Connector to Mux */
#define PS8740_REG_USB_EQ_RX     0x3b
#define PS8740_USB_EQ_RX_4_4_DB  0x00
#define PS8740_USB_EQ_RX_7_0_DB  0x10
#define PS8740_USB_EQ_RX_8_2_DB  0x20
#define PS8740_USB_EQ_RX_9_4_DB  0x30
#define PS8740_USB_EQ_RX_10_2_DB 0x40
#define PS8740_USB_EQ_RX_11_4_DB 0x50
#define PS8740_USB_EQ_RX_14_3_DB 0x60
#define PS8740_USB_EQ_RX_14_8_DB 0x70
#define PS8740_USB_EQ_RX_15_2_DB 0x80
#define PS8740_USB_EQ_RX_15_5_DB 0x90
#define PS8740_USB_EQ_RX_16_2_DB 0xa0
#define PS8740_USB_EQ_RX_17_3_DB 0xb0
#define PS8740_USB_EQ_RX_18_4_DB 0xc0
#define PS8740_USB_EQ_RX_20_1_DB 0xd0
#define PS8740_USB_EQ_RX_21_3_DB 0xe0

int ps8740_tune_usb_eq(int i2c_addr, uint8_t tx, uint8_t rx);

#endif /* __CROS_EC_PS8740_H */
