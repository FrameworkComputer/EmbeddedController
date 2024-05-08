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

ZTEST_SUITE(ucsi, NULL, NULL, NULL, NULL, NULL);

ZTEST_USER(ucsi, test_ucsi_command_names)
{
	enum ucsi_command_t cmd;

	for (cmd = 0; cmd <= UCSI_GET_LPM_PPM_INFO; cmd++) {
		if (cmd == 0x00 || cmd == 0x0a || cmd == 0x17) {
			zassert_is_null(
				get_ucsi_command_name(cmd),
				"Obsolete or Reserved UCSI command %d used",
				cmd);
		} else {
			zassert_not_null(get_ucsi_command_name(cmd),
					 "UCSI command %d missing name", cmd);
		}
	}
}
