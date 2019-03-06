/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Driver header for Intel Burnside Bridge - Thunderbolt/USB/DisplayPort Retimer
 */

#ifndef __CROS_EC_BB_RETIMER_H
#define __CROS_EC_BB_RETIMER_H

#include "gpio.h"

/* Burnside Bridge I2C Configuration Space */
#define BB_RETIMER_REG_VENDOR_ID	0
#define BB_RETIMER_VENDOR_ID		0x8086

#define BB_RETIMER_REG_DEVICE_ID	1
#define BB_RETIMER_DEVICE_ID		0x15EE

/* Connection State Register Attributes */
#define BB_RETIMER_REG_CONNECTION_STATE	4
#define BB_RETIMER_DATA_CONNECTION_PRESENT	BIT(0)
#define BB_RETIMER_CONNECTION_ORIENTATION	BIT(1)
#define BB_RETIMER_ACTIVE_CABLE			BIT(2)
#define BB_RETIMER_USB_3_CONNECTION		BIT(5)
#define BB_RETIMER_USB_DATA_ROLE		BIT(7)
#define BB_RETIMER_DP_CONNECTION		BIT(8)
#define BB_RETIMER_DP_PIN_ASSIGNMENT		BIT(10)
#define BB_RETIMER_DEBUG_ACCESSORY_MODE		BIT(12)
#define BB_RETIMER_IRQ_HPD			BIT(14)
#define BB_RETIMER_HPD_LVL			BIT(15)

/* Describes a USB Retimer present in the system */
struct bb_retimer {
	/* USB Retimer I2C port */
	const int i2c_port;

	/* USB Retimer I2C address */
	const int i2c_addr;

	/* NVM flag if shared with multiple BB-retimers */
	uint8_t shared_nvm;

	/* Retimer control GPIOs */
	enum gpio_signal usb_ls_en_gpio;	/* Load switch enable */
	enum gpio_signal retimer_rst_gpio;	/* Retimer reset */
	enum gpio_signal force_power_gpio;	/* Force power (active/low) */
};

/*
 * USB Retimers in system, ordered by PD port #, defined at board-level
 * CONFIG_USB_PD_RETIMER_INTEL_BB need to be defind at board-level.
 */
extern struct bb_retimer bb_retimers[];

#endif /* __CROS_EC_BB_RETIMER_H */
