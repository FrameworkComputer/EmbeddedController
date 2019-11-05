/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Driver for the Goodix GT7288 touch controller. */

#ifndef __CROS_EC_TOUCHPAD_GT7288_H
#define __CROS_EC_TOUCHPAD_GT7288_H

#include <stdbool.h>
#include <stddef.h>

/* The maximum number of contacts that can be reported at once. */
#define GT7288_MAX_CONTACTS 5

/**
 * Version information for the chip.
 */
struct gt7288_version_info {
	/** HID product ID (0x01F0 for touchpads, 0x01F1 for touchscreens). */
	uint16_t product_id;
	/**
	 * The firmware version. For touchpads equipped with a fingerprint
	 * sensor, the MSB will be 1.
	 */
	uint16_t version_id;
};

/**
 * Reads version information from the GT7288.
 *
 * @param[out] info The version information.
 *
 * @return EC_SUCCESS or an error code.
 */
int gt7288_get_version_info(struct gt7288_version_info *info);

/**
 * Data describing a single contact.
 */
struct gt7288_contact {
	/**
	 * A 4-bit ID that uniquely identifies the contact during its lifecycle.
	 */
	uint8_t id;
	/** The absolute X coordinate. */
	uint16_t x;
	/** The absolute Y coordinate. */
	uint16_t y;
	/** The width of the contact (with firmware version 4 or greater). */
	uint8_t width;
	/** The height of the contact (with firmware version 4 or greater). */
	uint8_t height;
	/** Whether the finger is touching the pad. (Currently always true.) */
	bool tip;
	/** Whether the touch is a finger (true) or palm (false). */
	bool confidence;
};

/**
 * Data from a complete report in PTP mode.
 */
struct gt7288_ptp_report {
	/** A relative timestamp, in units of 100µs. */
	uint16_t timestamp;
	/** The number of contacts on the pad. */
	size_t num_contacts;
	/** Whether the button is pressed. */
	bool button_down;
	/** An array of structs describing the individual contacts. */
	struct gt7288_contact contacts[GT7288_MAX_CONTACTS];
};

/**
 * Reads a complete report, when the GT7288 is in PTP mode.
 *
 * @param[out] report The report that is read.
 *
 * @return EC_SUCCESS or an error code.
 */
int gt7288_read_ptp_report(struct gt7288_ptp_report *report);

#endif /* __CROS_EC_TOUCHPAD_GT7288_H */
