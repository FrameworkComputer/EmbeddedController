/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ANX3443: 10G Active Mux (6x4) with
 * Integrated Re-timers for USB3.2/DisplayPort
 */

#ifndef __CROS_EC_USB_MUX_ANX3443_H
#define __CROS_EC_USB_MUX_ANX3443_H

#define ANX3443_I2C_READY_DELAY		(30 * MSEC)

/* I2C interface addresses */
#define ANX3443_I2C_ADDR0_FLAGS		0x10
#define ANX3443_I2C_ADDR1_FLAGS		0x14
#define ANX3443_I2C_ADDR2_FLAGS		0x16
#define ANX3443_I2C_ADDR3_FLAGS		0x11

/* This register is not documented in datasheet. */
#define ANX3443_REG_POWER_CNTRL		0x2B
#define ANX3443_POWER_CNTRL_OFF		0xFF


/* Ultra low power control register  */
#define ANX3443_REG_ULTRA_LOW_POWER	0xE6
#define ANX3443_ULTRA_LOW_POWER_EN	0x06
#define ANX3443_ULTRA_LOW_POWER_DIS	0x00

/* Mux control register  */
#define ANX3443_REG_ULP_CFG_MODE	0xF8
#define ANX3443_ULP_CFG_MODE_EN		BIT(4)
#define ANX3443_ULP_CFG_MODE_SWAP	BIT(3)
#define ANX3443_ULP_CFG_MODE_FLIP	BIT(2)
#define ANX3443_ULP_CFG_MODE_DP_EN	BIT(1)
#define ANX3443_ULP_CFG_MODE_USB_EN	BIT(0)

extern const struct usb_mux_driver anx3443_usb_mux_driver;

#endif /* __CROS_EC_USB_MUX_ANX3443_H */
