/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Driver header for Intel Burnside Bridge - Thunderbolt/USB/DisplayPort Retimer
 */

#ifndef __CROS_EC_BB_RETIMER_H
#define __CROS_EC_BB_RETIMER_H

#include "gpio.h"
#include "usb_mux.h"

/* Burnside Bridge I2C Configuration Space */
#define BB_RETIMER_REG_VENDOR_ID	0
#define BB_RETIMER_VENDOR_ID		0x8086

#define BB_RETIMER_REG_DEVICE_ID	1
#define BB_RETIMER_DEVICE_ID		0x15EE

/* Connection State Register Attributes */
#define BB_RETIMER_REG_CONNECTION_STATE		4
#define BB_RETIMER_DATA_CONNECTION_PRESENT	BIT(0)
#define BB_RETIMER_CONNECTION_ORIENTATION	BIT(1)
#define BB_RETIMER_RE_TIMER_DRIVER		BIT(2)
#define BB_RETIMER_USB_2_CONNECTION		BIT(4)
#define BB_RETIMER_USB_3_CONNECTION		BIT(5)
#define BB_RETIMER_USB_3_SPEED			BIT(6)
#define BB_RETIMER_USB_DATA_ROLE		BIT(7)
#define BB_RETIMER_DP_CONNECTION		BIT(8)
#define BB_RETIMER_DP_PIN_ASSIGNMENT		BIT(10)
#define BB_RETIMER_IRQ_HPD			BIT(14)
#define BB_RETIMER_HPD_LVL			BIT(15)
#define BB_RETIMER_TBT_CONNECTION		BIT(16)
#define BB_RETIMER_TBT_TYPE			BIT(17)
#define BB_RETIMER_TBT_CABLE_TYPE		BIT(18)
#define BB_RETIMER_VPRO_DOCK_DP_OVERDRIVE	BIT(19)
#define BB_RETIMER_TBT_ACTIVE_LINK_TRAINING	BIT(20)
#define BB_RETIMER_ACTIVE_PASSIVE		BIT(22)
#define BB_RETIMER_USB4_ENABLED			BIT(23)
#define BB_RETIMER_USB4_TBT_CABLE_SPEED_SUPPORT(x)	(((x) & 0x7) << 25)
#define BB_RETIMER_TBT_CABLE_GENERATION(x)		(((x) & 0x3) << 28)

/* Supported USB retimer drivers */
extern const struct usb_mux_driver bb_usb_retimer;

/* Retimer driver hardware specific controls */
struct bb_usb_control {
	/* Load switch enable */
	enum gpio_signal usb_ls_en_gpio;
	/* Retimer reset */
	enum gpio_signal retimer_rst_gpio;
};

#ifndef CONFIG_USBC_RETIMER_INTEL_BB_RUNTIME_CONFIG
extern const struct bb_usb_control bb_controls[];
#else
extern struct bb_usb_control bb_controls[];
#endif

/**
 * Handle the power state of BB retimer
 *
 * Define override function at board level if the platform specific changes
 * are needed to handle the power state of BB retimer.
 *
 * @param me     Pointer to USB mux
 * @param on_off BB retimer state to be changed
 *
 */
__override_proto void bb_retimer_power_handle(const struct usb_mux *me,
						int on_off);

#endif /* __CROS_EC_BB_RETIMER_H */
