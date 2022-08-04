/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Unit Tests for watchdog.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>

#include <zephyr/logging/log.h>
#include <zephyr/zephyr.h>
#include <zephyr/ztest.h>

#include "common.h"
#include "ec_tasks.h"
#include <zephyr/fff.h>
#include "hooks.h"
#include "test/drivers/stubs.h"
#include "watchdog.h"
#include "test/drivers/test_state.h"

#define wdt DEVICE_DT_GET(DT_CHOSEN(cros_ec_watchdog))

/**
 * @brief Default watchdog timeout plus some time for it to expire.
 */
#define DEFAULT_WDT_EXPIRY_MS \
	(CONFIG_AUX_TIMER_PERIOD_MS + (CONFIG_AUX_TIMER_PERIOD_MS / 2))

/**
 * @brief Boolean to indicate watchdog alert triggered
 */
bool wdt_warning_triggered;

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
	(void)watchdog_init();
	(void)wdt_setup(wdt, 0);
}

/**
 * @brief Watchdog test teardown handler.
 */
static void watchdog_after(void *state)
{
	ARG_UNUSED(state);
	wdt_warning_triggered = false;
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
ZTEST(watchdog, test_watchdog_init)
{
	int retval = EC_SUCCESS;

	/* Test already initialized (initialized in watchdog_before) */
	retval = watchdog_init();
	zassert_equal(-ENOMEM, retval, "Expected -ENOMEM, returned %d.",
		      retval);
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
ZTEST(watchdog, test_watchdog_reload)
{
	int i;
	int safe_wait_ms = DEFAULT_WDT_EXPIRY_MS / 2;

	zassert_false(wdt_warning_triggered, "Watchdog timer expired early.");
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
ZTEST(watchdog, test_wdt_warning_handler)
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
ZTEST_SUITE(watchdog, drivers_predicate_post_main, NULL, watchdog_before,
	    watchdog_after, NULL);
