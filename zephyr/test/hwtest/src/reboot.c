/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "multistep_test.h"
#include "system.h"

#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(reboot_hw_test, LOG_LEVEL_INF);

static void trigger_reboot(void)
{
	LOG_INF("Triggering reboot...");
	cflush();
	system_reset(SYSTEM_RESET_HARD);

	zassert_unreachable();
}

static void check_reboot(void)
{
	uint32_t flags = system_get_reset_flags();

	LOG_INF("Reset flags: 0x%x", flags);

	zassert_true(flags & EC_RESET_FLAG_HARD,
		     "Hard reset flag not set: 0x%x", flags);
}

static void (*test_steps[])(void) = { trigger_reboot, check_reboot };

MULTISTEP_TEST(reboot, test_steps);
