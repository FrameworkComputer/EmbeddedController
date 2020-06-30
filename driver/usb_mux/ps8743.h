/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Parade PS8743 USB Type-C Redriving Switch for USB Host / DisplayPort.
 */

#ifndef __CROS_EC_PS8743_H
#define __CROS_EC_PS8743_H

#include "usb_mux.h"

#define PS8743_I2C_ADDR0_FLAG    0x10
#define PS8743_I2C_ADDR1_FLAG    0x11
#define PS8743_I2C_ADDR2_FLAG    0x19
#define PS8743_I2C_ADDR3_FLAG    0x1a

/* Mode register for setting mux */
#define PS8743_REG_MODE 0x00
#define PS8743_MODE_IN_HPD_ASSERT    BIT(0)
#define PS8743_MODE_IN_HPD_CONTROL   BIT(1)
#define PS8743_MODE_FLIP_ENABLE      BIT(2)
#define PS8743_MODE_FLIP_REG_CONTROL BIT(3)
#define PS8743_MODE_USB_ENABLE       BIT(4)
#define PS8743_MODE_USB_REG_CONTROL  BIT(5)
#define PS8743_MODE_DP_ENABLE        BIT(6)
#define PS8743_MODE_DP_REG_CONTROL   BIT(7)
/* To reset the state machine to default */
#define PS8743_MODE_POWER_DOWN (PS8743_MODE_USB_REG_CONTROL |  \
				PS8743_MODE_DP_REG_CONTROL)

/* Status register for checking mux state */
#define PS8743_REG_STATUS 0x09
#define PS8743_STATUS_POLARITY_INVERTED BIT(2)
#define PS8743_STATUS_USB_ENABLED       BIT(3)
#define PS8743_STATUS_DP_ENABLED        BIT(4)
#define PS8743_STATUS_HPD_ASSERTED      BIT(7)

/* Chip ID / revision registers and expected fused values */
#define PS8743_REG_REVISION_ID1 0xf0
#define PS8743_REG_REVISION_ID2 0xf1
#define PS8743_REG_CHIP_ID1     0xf2
#define PS8743_REG_CHIP_ID2     0xf3
#define PS8743_REVISION_ID1_0   0x00
#define PS8743_REVISION_ID1_1   0x01
#define PS8743_REVISION_ID2     0x0b
#define PS8743_CHIP_ID1         0x41
#define PS8743_CHIP_ID2         0x87

/* USB equalization settings for Host to Mux */
#define PS8743_REG_USB_EQ_TX     0x32
#define PS8743_USB_EQ_TX_12_8_DB 0x00
#define PS8743_USB_EQ_TX_17_DB   0x20
#define PS8743_USB_EQ_TX_7_7_DB  0x40
#define PS8743_USB_EQ_TX_3_6_DB  0x60
#define PS8743_USB_EQ_TX_15_DB   0x80
#define PS8743_USB_EQ_TX_10_9_DB 0xc0
#define PS8743_USB_EQ_TX_4_5_DB  0xe0

/* USB equalization settings for Connector to Mux */
#define PS8743_REG_USB_EQ_RX     0x3b
#define PS8743_USB_EQ_RX_2_4_DB  0x00
#define PS8743_USB_EQ_RX_5_DB    0x10
#define PS8743_USB_EQ_RX_6_5_DB  0x20
#define PS8743_USB_EQ_RX_7_4_DB  0x30
#define PS8743_USB_EQ_RX_8_7_DB  0x40
#define PS8743_USB_EQ_RX_10_9_DB 0x50
#define PS8743_USB_EQ_RX_12_8_DB 0x60
#define PS8743_USB_EQ_RX_13_8_DB 0x70
#define PS8743_USB_EQ_RX_14_8_DB 0x80
#define PS8743_USB_EQ_RX_15_4_DB 0x90
#define PS8743_USB_EQ_RX_16_0_DB 0xa0
#define PS8743_USB_EQ_RX_16_7_DB 0xb0
#define PS8743_USB_EQ_RX_18_8_DB 0xc0
#define PS8743_USB_EQ_RX_21_3_DB 0xd0
#define PS8743_USB_EQ_RX_22_2_DB 0xe0

int ps8743_tune_usb_eq(int i2c_addr, uint8_t tx, uint8_t rx);
int ps8743_write(const struct usb_mux *me, uint8_t reg, uint8_t val);
int ps8743_read(const struct usb_mux *me, uint8_t reg, int *val);

#endif /* __CROS_EC_PS8743_H */
