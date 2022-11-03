/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#include "builtin/stdio.h"
#include "console.h"
#include "host_command.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

ZTEST_SUITE(console_cmd_ec_features, drivers_predicate_post_main, NULL, NULL,
	    NULL, NULL);

ZTEST_USER(console_cmd_ec_features, test_feat)
{
	char expected[50];
	int ret;

	ret = snprintf(expected, sizeof(expected),
		       " 0-31: 0x%08x\r\n32-63: 0x%08x", get_feature_flags0(),
		       get_feature_flags1());
	zassert_true(ret >= 0 && ret < sizeof(expected));

	CHECK_CONSOLE_CMD("feat", expected, EC_SUCCESS);
}
