/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * PS8802/PS8762 retimer.
 */
#include "usb_mux.h"

#ifndef __CROS_EC_USB_RETIMER_PS8802_H
#define __CROS_EC_USB_RETIMER_PS8802_H

/*
 * PS8802 uses 7-bit I2C addresses 0x08 to 0x17 (ADDR=L).
 * Page 0 = 0x08, Page 1 = 0x09, Page 2 = 0x0A.
 */
#define PS8802_I2C_ADDR_FLAGS	0x08

/*
 * PS8802 uses 7-bit I2C addresses 0x28 to 0x37.
 * Page 0 = 0x028, Page 1 = 0x29, Page 2 = 0x2A.
 */
#define PS8802_I2C_ADDR_FLAGS_CUSTOM	0x28

/*
 * PAGE 0 Register Definitions
 */
#define PS8802_REG_PAGE0	0x00

#define PS8802_REG0_TX_STATUS		0x72
#define PS8802_REG0_RX_STATUS		0x76
#define PS8802_STATUS_NORMAL_OPERATION		BIT(7)
#define PS8802_STATUS_10_GBPS			BIT(5)

/*
 * PAGE 1 Register Definitions
 */
#define PS8802_REG_PAGE1	0x01

#define PS8802_800MV_LEVEL_TUNING	0x8A
#define PS8802_EXTRA_SWING_LEVEL_P0_DEFAULT	0X00
#define PS8802_EXTRA_SWING_LEVEL_P0_DOWN_1	0X01
#define PS8802_EXTRA_SWING_LEVEL_P0_DOWN_2	0X02
#define PS8802_EXTRA_SWING_LEVEL_P0_DOWN_3	0X03
#define PS8802_EXTRA_SWING_LEVEL_P0_DOWN_4	0X04
#define PS8802_EXTRA_SWING_LEVEL_P0_UP_1	0X05
#define PS8802_EXTRA_SWING_LEVEL_P0_UP_2	0X06
#define PS8802_EXTRA_SWING_LEVEL_P0_UP_3	0X07
#define PS8802_EXTRA_SWING_LEVEL_P0_MASK	0X07

#define PS8802_REG_DCIRX		0x4B
#define PS8802_AUTO_DCI_MODE_DISABLE		BIT(7)
#define PS8802_FORCE_DCI_MODE			BIT(6)

/*
 * PAGE 2 Register Definitions
 */
#define PS8802_REG_PAGE2	0x02

#define PS8802_REG2_USB_SSEQ_LEVEL	0x02
#define PS8802_REG2_USB_CEQ_LEVEL	0x04
#define PS8802_USBEQ_LEVEL_UP_12DB		(0x0000 | 0x0003)
#define PS8802_USBEQ_LEVEL_UP_13DB		(0x0400 | 0x0007)
#define PS8802_USBEQ_LEVEL_UP_16DB		(0x0C00 | 0x000F)
#define PS8802_USBEQ_LEVEL_UP_17DB		(0x1C00 | 0x001F)
#define PS8802_USBEQ_LEVEL_UP_18DB		(0x3C00 | 0x003F)
#define PS8802_USBEQ_LEVEL_UP_19DB		(0x7C00 | 0x007F)
#define PS8802_USBEQ_LEVEL_UP_20DB		(0xFC00 | 0x00FF)
#define PS8802_USBEQ_LEVEL_UP_23DB		(0xFD00 | 0x01FF)
#define PS8802_USBEQ_LEVEL_UP_MASK		0xFDFF

#define PS8802_REG2_MODE		0x06
#define PS8802_MODE_DP_REG_CONTROL		BIT(7)
#define PS8802_MODE_DP_ENABLE			BIT(6)
#define PS8802_MODE_USB_REG_CONTROL		BIT(5)
#define PS8802_MODE_USB_ENABLE			BIT(4)
#define PS8802_MODE_FLIP_REG_CONTROL		BIT(3)
#define PS8802_MODE_FLIP_ENABLE			BIT(2)
#define PS8802_MODE_IN_HPD_REG_CONTROL		BIT(1)
#define PS8802_MODE_IN_HPD_ENABLE		BIT(0)

/*
 * Support power saving mode, Bit7 Disable
 * CE_DP, Bit5 Disable CE_USB, Bit3 Disable
 * FLIP pin, Bit1 Display IN_HPD pin, [Bit6 Bit4]
 * 00: I2C standy by mode.
 */
#define PS8802_MODE_STANDBY_MODE		0xAA

#define PS8802_REG2_DPEQ_LEVEL		0x07
#define PS8802_DPEQ_LEVEL_UP_9DB		0x00
#define PS8802_DPEQ_LEVEL_UP_11DB		0x01
#define PS8802_DPEQ_LEVEL_UP_12DB		0x02
#define PS8802_DPEQ_LEVEL_UP_14DB		0x03
#define PS8802_DPEQ_LEVEL_UP_17DB		0x04
#define PS8802_DPEQ_LEVEL_UP_18DB		0x05
#define PS8802_DPEQ_LEVEL_UP_19DB		0x06
#define PS8802_DPEQ_LEVEL_UP_20DB		0x07
#define PS8802_DPEQ_LEVEL_UP_21DB		0x08
#define PS8802_DPEQ_LEVEL_UP_MASK		0x0F

#define PS8802_P1_ADDR			0x0A
#define PS8802_ADDR_CFG			0xB0
#define PS8802_I2C_ADDR_FLAGS_ALT	0x50

extern const struct usb_mux_driver ps8802_usb_mux_driver;

int ps8802_i2c_wake(const struct usb_mux *me);
int ps8802_i2c_read(const struct usb_mux *me, int page, int offset, int *data);
int ps8802_i2c_write(const struct usb_mux *me, int page, int offset, int data);
int ps8802_i2c_write16(const struct usb_mux *me, int page, int offset,
			int data);
int ps8802_i2c_field_update8(const struct usb_mux *me, int page, int offset,
			     uint8_t field_mask, uint8_t set_value);
int ps8802_i2c_field_update16(const struct usb_mux *me, int page, int offset,
			     uint16_t field_mask, uint16_t set_value);
int ps8802_chg_i2c_addr(int i2c_port);

#endif /* __CROS_EC_USB_RETIMER_PS8802_H */
