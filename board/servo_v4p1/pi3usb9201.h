/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_PI3USB9201_H
#define __CROS_EC_PI3USB9201_H

enum pi3usb9201_reg_t {
	CTRL_REG1,
	CTRL_REG2,
	CLIENT_STATUS,
	HOST_STATUS
};

enum pi3usb9201_dat_t {
	POWER_DOWN	= 0x0,
	SDP_HOST_MODE	= 0x2,
	DCP_HOST_MODE	= 0x4,
	CDP_HOST_MODE	= 0x6,
	CLIENT_MODE	= 0x8,
	USB_PATH_ON	= 0xe
};


/* Client Status bits */
#define CS_DCP			BIT(7)
#define CS_SDP			BIT(6)
#define CS_CDP			BIT(5)
#define CS_1A_CHARGER		BIT(3)
#define CS_2A_CHARGER		BIT(2)
#define CS_2_4A_CHARGER		BIT(1)

/* Host Status bits */
#define HS_USB_UNPLUGGED	BIT(2)
#define HS_USB_PLUGGED		BIT(1)
#define HS_BC1_2		BIT(0)

/**
 * Selects Client Mode and client mode detection
 */
void init_pi3usb9201(void);

/**
 * Write a byte to the pi3usb9201
 *
 * @param reg	register to write
 * @param dat	data to write to the register
 */
void write_pi3usb9201(enum pi3usb9201_reg_t reg, enum pi3usb9201_dat_t dat);

/**
 * Read a byte from the pi3usb9201
 *
 * @param return	data byte read from pi3usb9201
 */
uint8_t read_pi3usb9201(enum pi3usb9201_reg_t reg);

#endif /* __CROS_EC_PI3USB9201_H */
