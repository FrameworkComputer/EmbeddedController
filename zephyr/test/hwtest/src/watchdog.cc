/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "multistep_test.h"
#include "panic.h"
#include "system.h"

#include <stdlib.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(watchdog_hw_test, LOG_LEVEL_INF);

static void test_watchdog(void)
{
	constexpr uint32_t watchdog_period_us =
		CONFIG_WATCHDOG_PERIOD_MS * USEC_PER_MSEC;

	/* Busy wait slightly longer than watchdog period to account for
	 * inaccuracies in busy waiting. */
	constexpr uint32_t us_to_wait = watchdog_period_us * 1.1;

	LOG_INF("Starting busy loop.");

	k_busy_wait(us_to_wait);

	/* Watchdog should fire before reaching this. */
	zassert_unreachable();
}

static void test_panic_data(void)
{
	uint32_t reason = 0;
	uint32_t info = 0;
	uint8_t exception = UINT8_MAX;

	panic_get_reason(&reason, &info, &exception);

	/* TODO(b/302354851): Only "reason" is set for watchdog in Zephyr. */
	zassert_equal(reason, PANIC_SW_WATCHDOG);
}

static void (*test_steps[])(void) = { test_watchdog, test_panic_data };

MULTISTEP_TEST(watchdog, test_steps)
