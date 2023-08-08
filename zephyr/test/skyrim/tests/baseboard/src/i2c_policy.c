/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "i2c.h"

#include <zephyr/ztest.h>

int board_allow_i2c_passthru(const struct i2c_cmd_desc_t *cmd_desc);

ZTEST_SUITE(i2c_policy, NULL, NULL, NULL, NULL, NULL);

ZTEST(i2c_policy, test_baseboard_suspend_change)
{
	/* Use our TCPC address as a test. */
	struct i2c_cmd_desc_t cmd_desc = {
		.port = 0,
		.addr_flags = 0x70,
	};
	zassert_equal(board_allow_i2c_passthru(&cmd_desc), 0);
}
