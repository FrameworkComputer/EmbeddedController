/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Parade PS874X USB Type-C Redriving Switch for USB Host / DisplayPort.
 */

#ifndef __CROS_EC_PS874X_H
#define __CROS_EC_PS874X_H

/* Mode register for setting mux */
#define PS874X_REG_MODE 0x00
#ifdef CONFIG_USB_MUX_PS8740
	#define PS874X_MODE_POLARITY_INVERTED BIT(4)
	#define PS874X_MODE_USB_ENABLED       BIT(5)
	#define PS874X_MODE_DP_ENABLED        BIT(6)
	#define PS874X_MODE_POWER_DOWN        BIT(7)
#elif defined(CONFIG_USB_MUX_PS8743)
	#define PS874X_MODE_POLARITY_INVERTED BIT(2)
	#define PS874X_MODE_FLIP_PIN_ENABLED  BIT(3)
	#define PS874X_MODE_USB_ENABLED       BIT(4)
	#define PS874X_MODE_CE_USB_ENABLED    BIT(5)
	#define PS874X_MODE_DP_ENABLED        BIT(6)
	#define PS874X_MODE_CE_DP_ENABLED     BIT(7)
	/* To reset the state machine to default */
	#define PS874X_MODE_POWER_DOWN (PS874X_MODE_CE_USB_ENABLED |  \
					PS874X_MODE_CE_DP_ENABLED)
#endif

/* Status register for checking mux state */
#define PS874X_REG_STATUS 0x09
#define PS874X_STATUS_POLARITY_INVERTED BIT(2)
#define PS874X_STATUS_USB_ENABLED       BIT(3)
#define PS874X_STATUS_DP_ENABLED        BIT(4)
#define PS874X_STATUS_HPD_ASSERTED      BIT(7)

/* Chip ID / revision registers and expected fused values */
#define PS874X_REG_REVISION_ID1 0xf0
#define PS874X_REG_REVISION_ID2 0xf1
#define PS874X_REG_CHIP_ID1     0xf2
#define PS874X_REG_CHIP_ID2     0xf3
#ifdef CONFIG_USB_MUX_PS8740
	#define PS874X_REVISION_ID1   0x00
	#define PS874X_REVISION_ID2   0x0a
	#define PS874X_CHIP_ID1       0x40
#elif defined(CONFIG_USB_MUX_PS8743)
	#define PS874X_REVISION_ID1_0 0x00
	#define PS874X_REVISION_ID1_1 0x01
	#define PS874X_REVISION_ID2 0x0b
	#define PS874X_CHIP_ID1     0x41
#endif
#define PS874X_CHIP_ID2         0x87

/* USB equalization settings for Host to Mux */
#define PS874X_REG_USB_EQ_TX     0x32
#ifdef CONFIG_USB_MUX_PS8740
	#define PS874X_USB_EQ_TX_10_1_DB 0x00
	#define PS874X_USB_EQ_TX_14_3_DB 0x20
	#define PS874X_USB_EQ_TX_8_5_DB  0x40
	#define PS874X_USB_EQ_TX_6_5_DB  0x60
	#define PS874X_USB_EQ_TX_11_5_DB 0x80
	#define PS874X_USB_EQ_TX_9_5_DB  0xc0
	#define PS874X_USB_EQ_TX_7_5_DB  0xe0
	#define PS874X_USB_EQ_TERM_100_OHM (0 << 2)
	#define PS874X_USB_EQ_TERM_85_OHM  BIT(2)
#elif defined(CONFIG_USB_MUX_PS8743)
	#define PS874X_USB_EQ_TX_12_8_DB 0x00
	#define PS874X_USB_EQ_TX_17_DB   0x20
	#define PS874X_USB_EQ_TX_7_7_DB  0x40
	#define PS874X_USB_EQ_TX_3_6_DB  0x60
	#define PS874X_USB_EQ_TX_15_DB   0x80
	#define PS874X_USB_EQ_TX_10_9_DB 0xc0
	#define PS874X_USB_EQ_TX_4_5_DB  0xe0
#endif

/* USB equalization settings for Connector to Mux */
#define PS874X_REG_USB_EQ_RX     0x3b
#ifdef CONFIG_USB_MUX_PS8740
	#define PS874X_USB_EQ_RX_4_4_DB  0x00
	#define PS874X_USB_EQ_RX_7_0_DB  0x10
	#define PS874X_USB_EQ_RX_8_2_DB  0x20
	#define PS874X_USB_EQ_RX_9_4_DB  0x30
	#define PS874X_USB_EQ_RX_10_2_DB 0x40
	#define PS874X_USB_EQ_RX_11_4_DB 0x50
	#define PS874X_USB_EQ_RX_14_3_DB 0x60
	#define PS874X_USB_EQ_RX_14_8_DB 0x70
	#define PS874X_USB_EQ_RX_15_2_DB 0x80
	#define PS874X_USB_EQ_RX_15_5_DB 0x90
	#define PS874X_USB_EQ_RX_16_2_DB 0xa0
	#define PS874X_USB_EQ_RX_17_3_DB 0xb0
	#define PS874X_USB_EQ_RX_18_4_DB 0xc0
	#define PS874X_USB_EQ_RX_20_1_DB 0xd0
	#define PS874X_USB_EQ_RX_21_3_DB 0xe0
#elif defined(CONFIG_USB_MUX_PS8743)
	#define PS874X_USB_EQ_RX_2_4_DB  0x00
	#define PS874X_USB_EQ_RX_5_DB    0x10
	#define PS874X_USB_EQ_RX_6_5_DB  0x20
	#define PS874X_USB_EQ_RX_7_4_DB  0x30
	#define PS874X_USB_EQ_RX_8_7_DB  0x40
	#define PS874X_USB_EQ_RX_10_9_DB 0x50
	#define PS874X_USB_EQ_RX_12_8_DB 0x60
	#define PS874X_USB_EQ_RX_13_8_DB 0x70
	#define PS874X_USB_EQ_RX_14_8_DB 0x80
	#define PS874X_USB_EQ_RX_15_4_DB 0x90
	#define PS874X_USB_EQ_RX_16_0_DB 0xa0
	#define PS874X_USB_EQ_RX_16_7_DB 0xb0
	#define PS874X_USB_EQ_RX_18_8_DB 0xc0
	#define PS874X_USB_EQ_RX_21_3_DB 0xd0
	#define PS874X_USB_EQ_RX_22_2_DB 0xe0
#endif

int ps874x_tune_usb_eq(int i2c_addr, uint8_t tx, uint8_t rx);

#endif /* __CROS_EC_PS874X_H */
