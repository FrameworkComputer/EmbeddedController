/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ANX7451: 10G Active Mux (4x4) with
 * Integrated Re-timers for USB3.2/DisplayPort
 */

#ifndef __CROS_EC_USB_MUX_ANX7451_H
#define __CROS_EC_USB_MUX_ANX7451_H

/* I2C interface addresses */
#define ANX7451_I2C_ADDR0_FLAGS		0x10
#define ANX7451_I2C_ADDR1_FLAGS		0x14
#define ANX7451_I2C_ADDR2_FLAGS		0x16
#define ANX7451_I2C_ADDR3_FLAGS		0x11

/* This register is not documented in datasheet. */
#define ANX7451_REG_POWER_CNTRL		0x2B
#define ANX7451_POWER_CNTRL_OFF		0xFF

/*
 * Ultra low power control register.
 * On ANX7451, this register should always be 0 (disabled).
 * See figure 2-2 in family programming guide.
 */
#define ANX7451_REG_ULTRA_LOW_POWER	0xE6
/* #define ANX7451_ULTRA_LOW_POWER_EN	0x06 */
#define ANX7451_ULTRA_LOW_POWER_DIS	0x00

/* Mux control register  */
#define ANX7451_REG_ULP_CFG_MODE	0xF8
#define ANX7451_ULP_CFG_MODE_EN		BIT(4)
#define ANX7451_ULP_CFG_MODE_SWAP	BIT(3)
#define ANX7451_ULP_CFG_MODE_FLIP	BIT(2)
#define ANX7451_ULP_CFG_MODE_DP_EN	BIT(1)
#define ANX7451_ULP_CFG_MODE_USB_EN	BIT(0)

extern const struct usb_mux_driver anx7451_usb_mux_driver;

#endif /* __CROS_EC_USB_MUX_ANX7451_H */
