/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Parade PS8740 (and PS8742)
 * USB Type-C Redriving Switch for USB Host / DisplayPort.
 */

#ifndef __CROS_EC_PS8740_H
#define __CROS_EC_PS8740_H

#include "usb_mux.h"

#define PS8740_I2C_ADDR0_FLAG    0x10
#define PS8740_I2C_ADDR1_FLAG    0x11
#define PS8740_I2C_ADDR2_FLAG    0x19
#define PS8740_I2C_ADDR3_FLAG    0x1a

/* Mode register for setting mux */
#define PS8740_REG_MODE 0x00
#define PS8740_MODE_POLARITY_INVERTED BIT(4)
#define PS8740_MODE_USB_ENABLED       BIT(5)
#define PS8740_MODE_DP_ENABLED        BIT(6)
#ifdef CONFIG_USB_MUX_PS8740
	#define PS8740_MODE_POWER_DOWN        BIT(7)
#elif defined(CONFIG_USB_MUX_PS8742)
	#define PS8740_MODE_CE_DP_ENABLED     BIT(7)
	/* To reset the state machine to default */
	#define PS8740_MODE_POWER_DOWN        0
#endif

/* Status register for checking mux state */
#define PS8740_REG_STATUS 0x09
#define PS8740_STATUS_POLARITY_INVERTED BIT(2)
#define PS8740_STATUS_USB_ENABLED       BIT(3)
#define PS8740_STATUS_DP_ENABLED        BIT(4)
#define PS8740_STATUS_HPD_ASSERTED      BIT(7)

/* Chip ID / revision registers and expected fused values */
#define PS8740_REG_REVISION_ID1 0xf0
#define PS8740_REG_REVISION_ID2 0xf1
#define PS8740_REG_CHIP_ID1     0xf2
#define PS8740_REG_CHIP_ID2     0xf3
#ifdef CONFIG_USB_MUX_PS8740
	#define PS8740_REVISION_ID1   0x00
	#define PS8740_REVISION_ID2_0 0x0a
	#define PS8740_REVISION_ID2_1 0x0b
	#define PS8740_CHIP_ID1       0x40
#elif defined(CONFIG_USB_MUX_PS8742)
	#define PS8740_REVISION_ID1   0x01
	#define PS8740_REVISION_ID2_0 0x0a
	#define PS8740_REVISION_ID2_1 0x0a
	#define PS8740_CHIP_ID1       0x42
#endif
#define PS8740_CHIP_ID2         0x87

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
#define PS8740_USB_EQ_TERM_85_OHM  BIT(2)

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
int ps8740_write(const struct usb_mux *me, uint8_t reg, uint8_t val);
int ps8740_read(const struct usb_mux *me, uint8_t reg, int *val);

#endif /* __CROS_EC_PS8740_H */
