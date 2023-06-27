/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "host_command.h"
#include "system.h"
#include "system_fake.h"
#include "system_safe_mode.h"

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

ZTEST_USER(system_safe_mode_disabled, test_feature_not_present)
{
	struct ec_response_get_features feat;

	zassert_ok(ec_cmd_get_features(NULL, &feat), "Failed to get features");

	zassert_false(feat.flags[1] &
		      EC_FEATURE_MASK_1(EC_FEATURE_SYSTEM_SAFE_MODE));
}
ZTEST_SUITE(system_safe_mode_disabled, NULL, NULL, NULL, NULL, NULL);
