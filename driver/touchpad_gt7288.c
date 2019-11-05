/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdbool.h>

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "i2c.h"
#include "task.h"
#include "touchpad_gt7288.h"
#include "util.h"

/* Define this to enable various warning messages during report parsing. */
#undef DEBUG_CHECKS

#define CPRINTS(format, args...) cprints(CC_TOUCHPAD, format, ## args)

#define GT7288_SLAVE_ADDRESS 0x14

#define GT7288_REPORT_ID_PTP 0x04

#define GT7288_BUTTON_STATE_UP 0x80
#define GT7288_BUTTON_STATE_DOWN 0x81

#define GT7288_REG_HID_DESCRIPTOR 0x0001
#define GT7288_REG_REPORT_DESCRIPTOR 0x0002

#define GT7288_HID_DESCRIPTOR_LENGTH 0x1E
#define GT7288_REPORT_DESCRIPTOR_LENGTH 0x1AE
#define GT7288_REPORT_LENGTH 16

/**
 * Reads a descriptor using the Conventional Read Mode.
 *
 * @param[in]  register_id The register containing the descriptor to read.
 * @param[out] data The data that is read.
 * @param[in]  max_length The maximum length of data.
 *
 * @return EC_SUCCESS or an error code.
 */
static int gt7288_read_desc(uint16_t register_id, uint8_t *data,
			    size_t max_length)
{
	uint8_t reg_bytes[] = {
		register_id & 0xFF, (register_id & 0xFF00) >> 8
	};
	return i2c_xfer(CONFIG_TOUCHPAD_I2C_PORT, GT7288_SLAVE_ADDRESS,
			reg_bytes, sizeof(reg_bytes), data, max_length);
}

int gt7288_get_version_info(struct gt7288_version_info *info)
{
	uint8_t data[GT7288_HID_DESCRIPTOR_LENGTH];

	RETURN_ERROR(gt7288_read_desc(GT7288_REG_HID_DESCRIPTOR, data,
				      sizeof(data)));
	info->product_id = UINT16_FROM_BYTE_ARRAY_LE(data, 22);
	info->version_id = UINT16_FROM_BYTE_ARRAY_LE(data, 24);
	return EC_SUCCESS;
}

static void gt7288_translate_contact(const uint8_t *data,
				     struct gt7288_contact *contact)
{
	if (IS_ENABLED(DEBUG_CHECKS)) {
		uint8_t report_id = data[2];

		if (report_id != GT7288_REPORT_ID_PTP) {
			CPRINTS("WARNING: unexpected report ID 0x%02X (expected 0x%02X).",
				report_id, GT7288_REPORT_ID_PTP);
		}
	}

	contact->id = data[3] >> 4;
	/* Note: these bits appear to be in the wrong order in the programming
	 * guide, verified by experimentation.
	 */
	contact->tip = (data[3] & BIT(1)) >> 1;
	contact->confidence = data[3] & BIT(0);
	contact->x = UINT16_FROM_BYTE_ARRAY_LE(data, 4);
	contact->y = UINT16_FROM_BYTE_ARRAY_LE(data, 6);
	contact->width = data[12];
	contact->height = data[13];
}

static int gt7288_read(uint8_t *data, size_t max_length)
{
	return i2c_xfer(CONFIG_TOUCHPAD_I2C_PORT, GT7288_SLAVE_ADDRESS,
			NULL, 0, data, max_length);
}

int gt7288_read_ptp_report(struct gt7288_ptp_report *report)
{
	size_t i;
	uint8_t data[GT7288_REPORT_LENGTH];

	RETURN_ERROR(gt7288_read(data, sizeof(data)));
	report->timestamp = UINT16_FROM_BYTE_ARRAY_LE(data, 8);

	if (data[10] > GT7288_MAX_CONTACTS) {
		if (IS_ENABLED(DEBUG_CHECKS))
			CPRINTS("ERROR: too many contacts (%d > %d).",
				data[10], GT7288_MAX_CONTACTS);
		return EC_ERROR_HW_INTERNAL;
	}
	report->num_contacts = data[10];

	if (IS_ENABLED(DEBUG_CHECKS) && data[11] != GT7288_BUTTON_STATE_UP &&
	    data[11] != GT7288_BUTTON_STATE_DOWN) {
		CPRINTS("WARNING: unexpected button state 0x%02X (expected 0x%02X or 0x%02X).",
			data[11], GT7288_BUTTON_STATE_UP,
			GT7288_BUTTON_STATE_DOWN);
	}
	report->button_down = data[11] == GT7288_BUTTON_STATE_DOWN;

	gt7288_translate_contact(data, &report->contacts[0]);

	for (i = 1; i < report->num_contacts; i++) {
		RETURN_ERROR(gt7288_read(data, sizeof(data)));
		gt7288_translate_contact(data, &report->contacts[i]);
	}
	return EC_SUCCESS;
}

#ifdef CONFIG_CMD_GT7288
static int command_gt7288_read_desc(int argc, char **argv)
{
	uint16_t register_id;
	long parsed_arg;
	char *end;
	int i;
	uint8_t data[GT7288_HID_DESCRIPTOR_LENGTH];

	if (argc != 2)
		return EC_ERROR_PARAM_COUNT;

	parsed_arg = strtoi(argv[1], &end, 0);
	if (parsed_arg < 0 || parsed_arg > UINT16_MAX || end == argv[1])
		return EC_ERROR_PARAM1;
	register_id = parsed_arg;

	RETURN_ERROR(gt7288_read_desc(register_id, data, sizeof(data)));

	ccprintf("Data: ");
	for (i = 0; i < sizeof(data); i++)
		ccprintf("%02X ", data[i]);
	ccprintf("\n");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(gt7288_desc, command_gt7288_read_desc,
			"register",
			"Read a descriptor on the GT7288");

static int command_gt7288_read_report_descriptor(int argc, char **argv)
{
	int i;
	uint8_t data[64];
	size_t bytes_read = 0;

	if (argc != 1)
		return EC_ERROR_PARAM_COUNT;

	/* The report descriptor is bigger than the Maxim I2C code can handle in
	 * one go, so we have to split it into chunks.
	 */
	RETURN_ERROR(gt7288_read_desc(GT7288_REG_REPORT_DESCRIPTOR, NULL, 0));
	ccprintf("Report descriptor: ");
	while (bytes_read < GT7288_REPORT_DESCRIPTOR_LENGTH) {
		size_t bytes_to_read =
			MIN(GT7288_REPORT_DESCRIPTOR_LENGTH - bytes_read,
			    sizeof(data));
		RETURN_ERROR(gt7288_read(data, bytes_to_read));

		for (i = 0; i < sizeof(data); i++)
			ccprintf("%02X ", data[i]);

		bytes_read += bytes_to_read;
	}
	ccprintf("\n");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(gt7288_repdesc, command_gt7288_read_report_descriptor,
			"", "Read the report descriptor on the GT7288");

static int command_gt7288_ver(int argc, char **argv)
{
	struct gt7288_version_info info;

	if (argc != 1)
		return EC_ERROR_PARAM_COUNT;

	RETURN_ERROR(gt7288_get_version_info(&info));
	ccprintf("Product ID: 0x%04X\n", info.product_id);
	ccprintf("Version ID: 0x%04X\n", info.version_id);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(gt7288_ver, command_gt7288_ver, "",
			"Read version information from the GT7288");

static int command_gt7288_report(int argc, char **argv)
{
	int i;
	struct gt7288_ptp_report report;

	RETURN_ERROR(gt7288_read_ptp_report(&report));
	ccprintf("Timestamp %d, button %s, %d contacts\n", report.timestamp,
		 report.button_down ? "down" : "up", report.num_contacts);
	if (report.num_contacts == 0)
		return EC_SUCCESS;

	ccprintf("ID,    X,    Y, width, height, tip, confidence\n");
	for (i = 0; i < report.num_contacts; i++) {
		struct gt7288_contact *contact = &report.contacts[i];

		ccprintf("%2d, %4d, %4d, %5d, %6d, %3d, %10d\n", contact->id,
			 contact->x, contact->y, contact->width,
			 contact->height, contact->tip, contact->confidence);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(gt7288_rep, command_gt7288_report, "",
			"Read a report from the GT7288.");
#endif /* CONFIG_CMD_GT7288 */
