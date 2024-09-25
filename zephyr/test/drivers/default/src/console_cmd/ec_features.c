/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "builtin/stdio.h"
#include "console.h"
#include "host_command.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "zephyr/sys/util_macro.h"

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

ZTEST_SUITE(console_cmd_ec_features, drivers_predicate_post_main, NULL, NULL,
	    NULL, NULL);

ZTEST_USER(console_cmd_ec_features, test_feat)
{
	char expected[50];
	uint32_t flags0 = get_feature_flags0();
	uint32_t flags1 = get_feature_flags1();
	int ret;

	ret = snprintf(expected, sizeof(expected),
		       " 0-31: 0x%08x\r\n32-63: 0x%08x", flags0, flags1);
	zassert_true(ret >= 0 && ret < sizeof(expected));
	if (IS_ENABLED(CONFIG_PLATFORM_EC_KEYBOARD_STRAUSS))
		zassert_true(flags1 & EC_FEATURE_MASK_1(EC_FEATURE_STRAUSS));

	CHECK_CONSOLE_CMD("feat", expected, EC_SUCCESS);
}
