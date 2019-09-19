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
 * struct gt7288_version_info - version information for the chip.
 * @product_id: HID product ID (0x01F0 for touchpads, 0x01F1 for touchscreens).
 * @version_id: the firmware version. For touchpads equipped with a fingerprint
 *              sensor, the MSB will be 1.
 */
struct gt7288_version_info {
	uint16_t product_id;
	uint16_t version_id;
};

/**
 * gt7288_get_version_info() - Reads version information from the GT7288.
 * @info: the version information.
 *
 * Return: EC_SUCCESS or an error code.
 */
int gt7288_get_version_info(struct gt7288_version_info *info);

/**
 * struct gt7288_contact - data describing a single contact.
 * @id: a 4-bit ID that uniquely identifies the contact during its lifecycle.
 * @x: the absolute X coordinate.
 * @y: the absolute Y coordinate.
 * @width: the width of the contact (with firmware version 0x0004 or greater).
 * @height: the height of the contact (with firmware version 0x0004 or greater).
 * @tip: whether the fingertip is touching the pad. (Currently always true.)
 * @confidence: whether the controller considers the touch a finger (true) or
 *              palm (false).
 */
struct gt7288_contact {
	uint8_t id;
	uint16_t x;
	uint16_t y;
	uint8_t width;
	uint8_t height;
	bool tip;
	bool confidence;
};

/**
 * struct gt7288_report - data from a complete report in PTP mode.
 * @timestamp: a relative timestamp, in units of 100µs.
 * @num_contacts: the number of contacts on the pad.
 * @button_down: whether the button is pressed.
 * @contacts: an array of structs describing the individual contacts.
 */
struct gt7288_ptp_report {
	uint16_t timestamp;
	size_t num_contacts;
	bool button_down;
	struct gt7288_contact contacts[GT7288_MAX_CONTACTS];
};

/**
 * gt7288_read_ptp_report() - Reads a complete report, when the GT7288 is in PTP
 *                            mode.
 * @report: the report that is read.
 *
 * Return: EC_SUCCESS or an error code.
 */
int gt7288_read_ptp_report(struct gt7288_ptp_report *report);

#endif /* __CROS_EC_TOUCHPAD_GT7288_H */
