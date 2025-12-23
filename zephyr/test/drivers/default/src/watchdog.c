/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Unit Tests for watchdog.
 */

#include "common.h"
#include "ec_tasks.h"
#include "hooks.h"
#include "host_command.h"
#include "panic.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "watchdog.h"

#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

#define wdt DEVICE_DT_GET(DT_CHOSEN(cros_ec_watchdog))

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
ZTEST(watchdog, test_watchdog_init)
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
ZTEST(watchdog, test_watchdog_reload)
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
ZTEST(watchdog, test_wdt_warning_handler)
{
	uint32_t reason;
	uint32_t info;
	uint8_t exception;

	/* Feed the dog so timer is reset */
	watchdog_reload();

	zassert_false(wdt_warning_triggered, "Watchdog timer expired early.");

	k_timer_start(&ktimer, K_MSEC(DEFAULT_WDT_EXPIRY_MS), K_NO_WAIT);
	k_busy_wait(DEFAULT_WDT_EXPIRY_MS * 1000);
	k_timer_stop(&ktimer);

	zassert_true(wdt_warning_triggered, "Watchdog timer did not expire.");

	panic_get_reason(&reason, &info, &exception);

	zassert_equal(PANIC_SW_WATCHDOG_WARN, reason,
		      "Watchdog warning panic reason was not set");

	zassert_equal(task_get_current(), exception,
		      "Panic exception should match current task id");
}

/**
 * @brief TestPurpose: Verify watchdog info host command.
 *
 * @details
 * Validate watchdog info host command.
 *
 * Expected Results
 *  - Validate watchdog info host command executed successfully.
 *  - Validate watchdog info static values match expected values.
 *  - Validate watchdog info dynamic values are in expected range.
 *  - Validate watchdog info dynamic values are reset after reset.
 */
ZTEST(watchdog, test_hostcmd_watchdog_info)
{
	struct host_cmd_handler_args args;
	struct ec_params_hostcmd_watchdog_info params;
	struct ec_response_hostcmd_watchdog_info response_before_reset;
	struct ec_response_hostcmd_watchdog_info response_after_reset;
	struct ec_response_hostcmd_watchdog_info response_after_wait;
	int rv;

	// Wait 1 msec to ensure elapsed time is greater than 0
	k_sleep(K_MSEC(1));

	args.command = EC_CMD_HOSTCMD_WATCHDOG_INFO;
	args.version = 0;
	args.params = &params;
	args.params_size = sizeof(params);

	args.response = &response_before_reset;
	args.response_max = sizeof(response_before_reset);
	args.response_size = 0;
	params.reset_stats = 1;

	// Reset stats
	rv = host_command_process(&args);
	uint64_t ts_after_reset = k_uptime_get();
	zassert_equal(EC_RES_SUCCESS, rv, "Failed to get watchdog info");
	zassert_equal(sizeof(response_before_reset), args.response_size,
		      "Unexpected response size");

	args.response = &response_after_reset;
	args.response_max = sizeof(response_after_reset);
	args.response_size = 0;
	params.reset_stats = 0;
	// Reload watchdog once
	watchdog_reload();
	uint64_t ts_after_reload = k_uptime_get();
	rv = host_command_process(&args);
	zassert_equal(EC_RES_SUCCESS, rv, "Failed to get watchdog info");
	zassert_equal(sizeof(response_after_reset), args.response_size,
		      "Unexpected response size");

	uint64_t ts_before_wait = k_uptime_get();
	// Wait 3 ticks
	k_sleep(K_USEC(HOOK_TICK_INTERVAL * 3));
	args.response = &response_after_wait;
	args.response_max = sizeof(response_after_wait);
	args.response_size = 0;
	rv = host_command_process(&args);
	uint64_t ts_after_wait = k_uptime_get();
	zassert_equal(EC_RES_SUCCESS, rv, "Failed to get watchdog info");
	zassert_equal(sizeof(response_after_wait), args.response_size,
		      "Unexpected response size");

	/* Verify static values */
	zassert_equal(CONFIG_WATCHDOG_PERIOD_MS,
		      response_before_reset.watchdog_period_ms,
		      "Unexpected watchdog_period_ms");
	zassert_equal(CONFIG_AUX_TIMER_PERIOD_MS,
		      response_before_reset.watchdog_warning_period_ms,
		      "Unexpected watchdog_warning_period_ms");
	zassert_equal(HOOK_TICK_INTERVAL / USEC_PER_MSEC,
		      response_before_reset.watchdog_reload_period_nominal_ms,
		      "Unexpected watchdog_reload_period_nominal_ms");

	/* Verify dynamic stats are reset */
	zassert_not_equal(
		response_before_reset.watchdog_reload_period_max_ts_ms,
		response_after_reset.watchdog_reload_period_max_ts_ms);
	zassert_not_equal(response_before_reset.watchdog_reload_period_max_ms,
			  response_after_reset.watchdog_reload_period_max_ms);
	zassert_not_equal(response_before_reset.watchdog_stats_elapsed_ms,
			  response_after_reset.watchdog_stats_elapsed_ms);

	/* Verify static values are not reset */
	zassert_equal(response_before_reset.watchdog_period_ms,
		      response_after_reset.watchdog_period_ms);
	zassert_equal(response_before_reset.watchdog_warning_period_ms,
		      response_after_reset.watchdog_warning_period_ms);
	zassert_equal(response_before_reset.watchdog_reload_period_nominal_ms,
		      response_after_reset.watchdog_reload_period_nominal_ms);

	/* Verify dynamic stats after reset and one reload */
	zassert_equal(response_after_reset.watchdog_reload_count, 1);
	zassert_true(
		IN_RANGE(response_after_reset.watchdog_reload_period_max_ts_ms,
			 ts_after_reset, ts_after_reload),
		"Unexpected watchdog_reload_period_max_ts_ms");

	/* Verify dynamic values after 3 tick wait are in expected range */
	zassert_true(
		response_after_wait.watchdog_reload_period_max_ms >=
			response_after_wait.watchdog_reload_period_nominal_ms,
		"Unexpected watchdog_reload_period_max_ms");
	zassert_true(
		IN_RANGE(response_after_wait.watchdog_reload_period_max_ts_ms,
			 ts_before_wait, ts_after_wait),
		"Unexpected watchdog_reload_period_max_ts_ms");
	zassert_true(IN_RANGE(response_after_wait.watchdog_stats_elapsed_ms,
			      HOOK_TICK_INTERVAL * 3 / 1000,
			      HOOK_TICK_INTERVAL * 4 / 1000),
		     "Unexpected watchdog_stats_elapsed_ms");
	zassert_true(IN_RANGE(response_after_wait.watchdog_reload_count, 3, 5),
		     "Unexpected watchdog_reload_count");
}

/**
 * @brief TestPurpose: Verify watchdog info shell command.
 *
 * @details
 * Validate watchdog info shell command.
 *
 * Expected Results
 *  - Validate watchdog info shell command with no args executes successfully.
 *  - Validate watchdog info shell command with reset_stats arg executes
 * successfully.
 *  - Validate watchdog info shell command with invalid arg fails.
 */
ZTEST(watchdog, test_cmd_watchdoginfo)
{
	const struct shell *shell = shell_backend_dummy_get_ptr();

	/* Test default info command (no args) */
	zassert_equal(0, shell_execute_cmd(shell, "watchdoginfo"),
		      "watchdoginfo failed");

	/* Test reset_stats */
	zassert_equal(0, shell_execute_cmd(shell, "watchdoginfo reset_stats"),
		      "watchdoginfo reset_stats failed");

	/* Test invalid arg */
	zassert_not_equal(0, shell_execute_cmd(shell, "watchdoginfo invalid"),
			  "watchdoginfo invalid should fail");
}

/**
 * @brief Test Suite: Verifies watchdog functionality.
 */
ZTEST_SUITE(watchdog, drivers_predicate_post_main, NULL, watchdog_before, NULL,
	    NULL);
