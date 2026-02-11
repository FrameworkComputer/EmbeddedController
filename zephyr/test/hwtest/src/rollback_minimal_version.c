/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "rollback.h"

#include <zephyr/ztest.h>

ZTEST_SUITE(rollback_minimal_version, NULL, NULL, NULL, NULL, NULL);

/**
 * @brief Test that the current rollback minimal version matches the expected
 * value.
 *
 * This test verifies that the RO firmware has updated the rollback minimal
 * version in the rollback protection blocks to match the expected version
 * defined in the configuration.
 */
ZTEST(rollback_minimal_version, test_rollback_minimal_version)
{
	int32_t version;

	version = rollback_get_minimum_version();
	zassert_true(version >= 0, "Failed to get latest rollback data");

	zassert_equal(
		version, CONFIG_PLATFORM_EC_ROLLBACK_VERSION,
		"Rollback version (%d) does not match expected value (%d)",
		version, CONFIG_PLATFORM_EC_ROLLBACK_VERSION);
}
