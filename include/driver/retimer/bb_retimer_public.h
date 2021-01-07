/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Public header for Intel Burnside Bridge - Thunderbolt/USB/DisplayPort Retimer
 */

#ifndef __CROS_EC_DRIVER_RETIMER_BB_RETIMER_PUBLIC_H
#define __CROS_EC_DRIVER_RETIMER_BB_RETIMER_PUBLIC_H

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

#endif /* __CROS_EC_DRIVER_RETIMER_BB_RETIMER_PUBLIC_H */
