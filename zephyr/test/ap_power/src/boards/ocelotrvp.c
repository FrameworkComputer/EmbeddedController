/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Ocelot RVP board-specific AP power sequence tests.
 *
 * These tests exercise code paths in rvp_board_power.c that are not covered
 * by the generic ap_pwrseq test suite.
 */

#include "power_signals.h"
#include "test_state.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

/* Include this header last as it will override NULL in the state defines
 * with a valid value avoiding incorrect expansion of NULL in the test context.
 */
#include <ap_power/ap_pwrseq_sm.h>

/* Declared in rvp_board_power.c and compiled into the test binary when
 * CONFIG_TEST_AP_POWER_OCELOTRVP=y.
 */
extern void board_ap_power_force_shutdown(void);
extern int board_power_signal_get(enum power_signal signal);

/**
 * @brief Test that board_ap_power_force_shutdown properly sequences power off
 * signals.
 *
 * This test verifies that the force shutdown function follows the correct
 * sequence: de-assert PWR_PCH_PWROK and PWR_EC_PCH_SYS_PWROK, set
 * PWR_EC_PCH_RSMRST, disable PWR_EN_PP3300_A, and wait for PWR_RSMRST_PWRGD
 * to go low.
 */
ZTEST(ap_pwrseq_ocelotrvp_board, test_board_ap_power_force_shutdown)
{
	/* Ensure power signals start in a known state */
	power_signal_set(PWR_PCH_PWROK, 1);
	power_signal_set(PWR_EC_PCH_SYS_PWROK, 1);
	power_signal_set(PWR_EC_PCH_RSMRST, 0);
	power_signal_set(PWR_EN_PP3300_A, 1);

	/*
	 * board_ap_power_force_shutdown should:
	 * 1. De-assert PCH_PWROK and EC_PCH_SYS_PWROK
	 * 2. Assert PWR_EC_PCH_RSMRST (set to 1)
	 * 3. Disable PWR_EN_PP3300_A
	 * 4. Wait for PWR_RSMRST_PWRGD to go low with timeout
	 * 5. Wait the minimum power down delay
	 */
	board_ap_power_force_shutdown();

	/* Verify final state */
	zassert_equal(0, power_signal_get(PWR_PCH_PWROK),
		      "PWR_PCH_PWROK should be de-asserted");
	zassert_equal(0, power_signal_get(PWR_EC_PCH_SYS_PWROK),
		      "PWR_EC_PCH_SYS_PWROK should be de-asserted");
	zassert_equal(1, power_signal_get(PWR_EC_PCH_RSMRST),
		      "PWR_EC_PCH_RSMRST should be asserted");
	zassert_equal(0, power_signal_get(PWR_EN_PP3300_A),
		      "PWR_EN_PP3300_A should be disabled");
}

/**
 * @brief Test that board_ap_power_force_shutdown emits a warning when
 * PWR_RSMRST_PWRGD remains asserted after the 50 ms timeout.
 *
 * The normal emulator brings RSMRST low ~10 ms after PP3300_A is de-asserted,
 * so the timeout path (and the associated LOG_WRN) is never reached in
 * ordinary test runs. This test drives RSMRST high through the GPIO emulator
 * directly so that board_ap_power_force_shutdown has to spin through the full
 * timeout loop.
 */
ZTEST(ap_pwrseq_ocelotrvp_board, test_force_shutdown_rsmrst_timeout)
{
	const struct gpio_dt_spec rsmrst =
		GPIO_DT_SPEC_GET(DT_NODELABEL(pwr_rsmrst_pwrgd), gpios);

	/* Drive RSMRST_PWRGD high so it will not drop during force_shutdown. */
	gpio_emul_input_set(rsmrst.port, rsmrst.pin, 1);

	/* Ensure power signals start in a known state */
	power_signal_set(PWR_PCH_PWROK, 1);
	power_signal_set(PWR_EC_PCH_SYS_PWROK, 1);
	power_signal_set(PWR_EC_PCH_RSMRST, 0);
	power_signal_set(PWR_EN_PP3300_A, 1);

	/*
	 * board_ap_power_force_shutdown will poll RSMRST for up to
	 * X86_NON_DSX_FORCE_SHUTDOWN_TO_MS (50) ms, emit LOG_WRN, then wait
	 * the BOARD_OCELOT_MINIMUM_POWER_DOWN_DELAY_MS (30) ms rail-off delay.
	 */
	board_ap_power_force_shutdown();

	/* Restore RSMRST to de-asserted state for subsequent tests. */
	gpio_emul_input_set(rsmrst.port, rsmrst.pin, 0);
}

/**
 * @brief Test G3 state handling and power startup event processing.
 *
 * This test verifies that the G3 state machine properly handles power startup
 * events and transitions PWR_EN_PP3300_A appropriately.
 */
ZTEST(ap_pwrseq_ocelotrvp_board, test_board_ap_power_action_g3)
{
	const struct device *dev = ap_pwrseq_get_instance();

	/* Start in G3 state */
	ap_pwrseq_start(dev, AP_POWER_STATE_G3);

	/* Ensure PP3300_A starts disabled */
	power_signal_set(PWR_EN_PP3300_A, 0);

	/* Post a power startup event */
	ap_pwrseq_post_event(dev, AP_PWRSEQ_EVENT_POWER_STARTUP);

	/* Give time for event processing */
	k_msleep(10);

	/* Verify that PWR_EN_PP3300_A is now enabled */
	zassert_equal(1, power_signal_get(PWR_EN_PP3300_A),
		      "PWR_EN_PP3300_A should be enabled after power startup");
}

/**
 * @brief Test that board_power_signal_get returns correct values.
 *
 * For PWR_EC_PCH_SYS_PWROK, it should return the value of PWR_PCH_PWROK.
 * For other signals, it should return -EINVAL.
 */
ZTEST(ap_pwrseq_ocelotrvp_board, test_board_power_signal_get)
{
	/* Test PWR_EC_PCH_SYS_PWROK case */
	power_signal_set(PWR_PCH_PWROK, 1);
	zassert_equal(
		1, board_power_signal_get(PWR_EC_PCH_SYS_PWROK),
		"board_power_signal_get should return PWR_PCH_PWROK value");

	power_signal_set(PWR_PCH_PWROK, 0);
	zassert_equal(
		0, board_power_signal_get(PWR_EC_PCH_SYS_PWROK),
		"board_power_signal_get should return PWR_PCH_PWROK value");

	/* Test default case */
	zassert_equal(-EINVAL, board_power_signal_get(PWR_EN_PP3300_A),
		      "Expected -EINVAL for unsupported signal");
}

ZTEST_SUITE(ap_pwrseq_ocelotrvp_board, ap_power_predicate_post_main, NULL, NULL,
	    NULL, NULL);

/* Tests that run the power sequence up to S0 require a board-level S0 state
 * handler to assert PWR_PCH_PWROK, which the generic ap_pwrseq framework does
 * not provide for this board. Use 0 instead of NULL for entry/exit: the
 * nested macro expansion in AP_POWER_APP_STATE_DEFINE pre-expands NULL to
 * ((void*)0) in the test include context, which breaks ## token pasting.
 * 0 is a valid null pointer constant for the handler type and avoids this.
 */
static int board_ap_power_action_s0_run(void *data)
{
	power_signal_set(PWR_PCH_PWROK, 1);
	return 0;
}

AP_POWER_APP_STATE_DEFINE(S0, NULL, board_ap_power_action_s0_run, NULL);
