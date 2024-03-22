/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Unit Tests for watchdog helper.
 */

#include "common.h"
#include "ec_tasks.h"
#include "hooks.h"
#include "panic.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "watchdog.h"

#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#define wdt_helper DEVICE_DT_GET(DT_CHOSEN(cros_ec_watchdog_helper))

/**
 * @brief Default watchdog timeout plus some time for it to expire.
 */
#define DEFAULT_WDT_EXPIRY_MS \
	(CONFIG_AUX_TIMER_PERIOD_MS + (CONFIG_AUX_TIMER_PERIOD_MS / 2))

/**
 * @brief Boolean to indicate watchdog alert triggered
 */
extern bool wdt_warning_triggered;
bool wdt_initialized;

/**
 * @brief timer to used to validate watchdog expiries.
 */
K_TIMER_DEFINE(ktimer, NULL, NULL);

/**
 * @brief Watchdog test setup handler.
 */
static void watchdog_before(void *state)
{
	ARG_UNUSED(state);
	set_test_runner_tid();
	wdt_warning_triggered = false;

	/* When shuffling need watchdog initialized and running
	 * for other tests.
	 */
	if (!wdt_initialized) {
		(void)watchdog_init();
		wdt_initialized = true;
	}
}

/**
 * @brief TestPurpose: Verify watchdog initialization.
 *
 * @details
 * Validate watchdog initialization.
 *
 * Expected Results
 *  - Successful on first init.
 *  - Failure on second init.
 */
ZTEST(watchdog_helper, test_watchdog_init)
{
	int retval = EC_SUCCESS;

	/* Test already initialized (initialized in watchdog_before) */
	retval = watchdog_init();
	zassert_equal(-EBUSY, retval, "Expected -EBUSY, returned %d.", retval);
}

/**
 * @brief TestPurpose: Verify watchdog reload.
 *
 * @details
 * Validate watchdog is fed.
 *
 * Expected Results
 *  - watchdog warning handler function is never triggered
 */
ZTEST(watchdog_helper, test_watchdog_reload)
{
	int i;
	int safe_wait_ms = DEFAULT_WDT_EXPIRY_MS / 2;

	zassert_false(wdt_warning_triggered, "Watchdog timer expired early.");
	watchdog_reload();
	for (i = 0; i < 10; i++) {
		k_timer_start(&ktimer, K_MSEC(safe_wait_ms), K_NO_WAIT);
		k_busy_wait(safe_wait_ms * 1000);
		k_timer_stop(&ktimer);
		watchdog_reload();
		zassert_false(wdt_warning_triggered,
			      "Watchdog timer expired unexpectedly on loop=%d",
			      i);
	}
}

/**
 * @brief TestPurpose: Verify watchdog timer expires.
 *
 * @details
 * Validate watchdog timer expiry occurs after busy wait
 *
 * Expected Results
 *  - Validate watchdog warning handler function is triggered.
 */
ZTEST(watchdog_helper, test_wdt_warning_handler)
{
	/* Feed the dog so timer is reset */
	watchdog_reload();

	zassert_false(wdt_warning_triggered, "Watchdog timer expired early.");

	k_timer_start(&ktimer, K_MSEC(DEFAULT_WDT_EXPIRY_MS), K_NO_WAIT);
	k_busy_wait(DEFAULT_WDT_EXPIRY_MS * 1000);
	k_timer_stop(&ktimer);

	zassert_true(wdt_warning_triggered, "Watchdog timer did not expire.");
}

/**
 * @brief Test Suite: Verifies watchdog functionality.
 */
ZTEST_SUITE(watchdog_helper, drivers_predicate_post_main, NULL, watchdog_before,
	    NULL, NULL);
