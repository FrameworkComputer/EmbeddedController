/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Implementation of I2C HID for touchpads */
#ifndef __CROS_EC_I2C_HID_TOUCHPAD_H
#define __CROS_EC_I2C_HID_TOUCHPAD_H

#include "common.h"
#include "i2c_hid.h"
#include "stdbool.h"
#include "stdint.h"

/* Max fingers to support */
#define I2C_HID_TOUCHPAD_MAX_FINGERS	5

/*
 * Struct holding a touchpad event
 *
 * The user should parse the original touchpad report, apply necessary
 * transformations and fill the result in this common struct. The touchpad is
 * assumed to implement the Linux HID MT-B protocol.
 */
struct touchpad_event {
	/* If hover is detected */
	bool hover;
	/* If button is clicked */
	bool button;
	/* Struct for contacts */
	struct {
		/* X & Y of the contact */
		uint16_t x;
		uint16_t y;
		/* Pressure/contact area */
		uint16_t pressure;
		/* W & H of the contact */
		uint16_t width;
		uint16_t height;
		/*
		 * Orientation of the contact ellipse. Can be plain 0 if
		 * unavailable.
		 */
		uint16_t orientation;
		/*
		 * If the touchpad believes it is a palm. Some touchpads report
		 * it through the confidence field.
		 */
		bool is_palm;
		/* If this slot contains valid contact (touching the surface) */
		bool valid;
	} __packed finger[I2C_HID_TOUCHPAD_MAX_FINGERS];
} __packed;

#endif /* __CROS_EC_I2C_HID_TOUCHPAD_H */
