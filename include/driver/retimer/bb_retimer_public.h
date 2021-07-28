/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Public header for Intel Burnside Bridge - Thunderbolt/USB/DisplayPort Retimer
 */

#ifndef __CROS_EC_DRIVER_RETIMER_BB_RETIMER_PUBLIC_H
#define __CROS_EC_DRIVER_RETIMER_BB_RETIMER_PUBLIC_H

struct usb_mux;

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
 * Enable/disable the power state of BB retimer
 *
 * Define override function at board level if the platform specific changes
 * are needed to enable/disable the power state of BB retimer.
 *
 * @param me     Pointer to USB mux
 * @param enable BB retimer power state to be changed
 *
 * @return EC_SUCCESS, or non-zero on error.
 */
__override_proto int bb_retimer_power_enable(const struct usb_mux *me,
					     bool enable);

/**
 * reset the BB retimer
 *
 * Define override function at board level if the platform specific changes
 * are needed to reset the BB retimer.
 *
 * @param me     Pointer to USB mux
 *
 * @return EC_SUCCESS, or non-zero on error.
 */
__override_proto int bb_retimer_reset(const struct usb_mux *me);

#endif /* __CROS_EC_DRIVER_RETIMER_BB_RETIMER_PUBLIC_H */
