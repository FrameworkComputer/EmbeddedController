/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/ucsi_v3.h"
#include "zephyr/sys/util.h"
#include "zephyr/sys/util_macro.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

extern const char *const ucsi_invalid_name;
extern const char *const ucsi_deprecated_name;

ZTEST_SUITE(ucsi, NULL, NULL, NULL, NULL, NULL);

ZTEST_USER(ucsi, test_ucsi_command_names)
{
	enum ucsi_command_t cmd;

	for (cmd = 0; cmd < UCSI_CMD_MAX; cmd++) {
		if (cmd == 0x00 || cmd == 0x0a || cmd == 0x17) {
			zassert_equal(
				get_ucsi_command_name(cmd),
				ucsi_deprecated_name,
				"Obsolete or Reserved UCSI command %d not identified",
				cmd);
		} else {
			zassert_not_null(get_ucsi_command_name(cmd),
					 "UCSI command %d missing name", cmd);
		}
	}

	zassert_equal(get_ucsi_command_name(UCSI_CMD_MAX), ucsi_invalid_name);
}

/* Test mapping of notification bits to connection status change bits. */
ZTEST_USER(ucsi, test_notification_bit_mapping)
{
	union notification_enable_t notify;
	union conn_status_change_bits_t status = { 0 };

	struct notify_status_map {
		uint32_t notify;
		uint16_t status;
	} notify_to_status_tests[] = {
		/* All bits except sink path are set. */
		{ .notify = 0x0000FFFF, .status = 0xDBEE },

		/* Set sink path only. */
		{ .notify = 0x00010000, .status = 0x2000 },

		/* Re-timer mode bit in notify overlaps with sink path in
		 * status.
		 */
		{ .notify = 0x00012000, .status = 0x2000 },
	};

	for (int i = 0; i < ARRAY_SIZE(notify_to_status_tests); ++i) {
		notify.raw_value = notify_to_status_tests[i].notify;
		status = conn_status_mask_from_notification(notify);

		zassert_equal(status.raw_value,
			      notify_to_status_tests[i].status);
	}
}
