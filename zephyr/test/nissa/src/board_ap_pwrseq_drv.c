/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * AP Power sequence driver unit tests for program/nissa/src/board_power.c.
 * Nissa only has action handlers for power state G3 and S0.
 */
#include "ap_power/ap_power.h"
#include "emul/emul_power_signals.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

#include <ap_power/ap_pwrseq_sm.h>
#include <ap_power_override_functions.h>
#include <common.h>
#include <power_signals.h>

int chipset_s0_run_count;

static void setup_test(void *fixture)
{
	power_signal_init();
}

static void after_test(void *fixture)
{
	power_signal_emul_unload();
	chipset_s0_run_count = 0;
}

ZTEST_SUITE(nissa_board_power, NULL, setup_test, NULL, after_test, NULL);

/*
 * @brief TestPurpose: Check G3 power state entry.
 *
 * @details
 * Ensure that AP power sequence driver is not initialized until
 * `ap_pwrseq_start` is called and G3 entry sets power signals properly.
 *
 * Expected Results:
 * - AP power sequence driver is not started.
 * - `ap_pwrseq_start` starts AP power sequence driver.
 * - G3 action handler is called and power signals are set as expected.
 */
ZTEST(nissa_board_power, test_board_ap_power_g3_run_0)
{
	const struct device *dev = ap_pwrseq_get_instance();

	zassert_equal(0,
		      power_signal_emul_load(EMUL_POWER_SIGNAL_TEST_PLATFORM(
			      tp_power_down_ok)),
		      "Unable to load test platform `tp_power_down_ok`");
	zassert_equal(0, power_signal_get(PWR_SLP_SUS));
	zassert_equal(1, power_signal_get(PWR_RSMRST_PWRGD));
	zassert_equal(1, power_signal_get(PWR_EN_PP3300_A));
	zassert_equal(1, power_signal_get(PWR_EC_SOC_DSW_PWROK));
	zassert_equal(0, power_signal_get(PWR_EC_PCH_RSMRST));
	zassert_ok(ap_pwrseq_start(dev, AP_POWER_STATE_G3),
		   "Driver already initialized");
	zassert_equal(1, power_signal_get(PWR_SLP_SUS));
	zassert_equal(0, power_signal_get(PWR_RSMRST_PWRGD));
	zassert_equal(0, power_signal_get(PWR_EN_PP3300_A));
	zassert_equal(0, power_signal_get(PWR_EC_SOC_DSW_PWROK));
	zassert_equal(1, power_signal_get(PWR_EC_PCH_RSMRST));
}

/*
 * @brief TestPurpose: Check G3 power handler response.
 *
 * @details
 * G3 action handler does not respond to `AP_PWRSEQ_EVENT_POWER_SIGNAL` event.
 *
 * Expected Results:
 * - Current power state is G3.
 * - G3 action handler does not modify power signals when event
 * `AP_PWRSEQ_EVENT_POWER_SIGNAL` is posted.
 */
ZTEST(nissa_board_power, test_board_ap_power_g3_run_1)
{
	const struct device *dev = ap_pwrseq_get_instance();

	zassert_equal(0,
		      power_signal_emul_load(
			      EMUL_POWER_SIGNAL_TEST_PLATFORM(tp_power_up_ok)),
		      "Unable to load test platform `tp_power_up_ok`");

	zassert_equal(ap_pwrseq_get_current_state(dev), AP_POWER_STATE_G3);
	zassert_equal(0, power_signal_get(PWR_EN_PP3300_A));
	zassert_equal(0, power_signal_get(PWR_EN_PP5000_A));
	zassert_equal(0, power_signal_get(PWR_DSW_PWROK));
	ap_pwrseq_post_event(dev, AP_PWRSEQ_EVENT_POWER_SIGNAL);
	zassert_equal(ap_pwrseq_get_current_state(dev), AP_POWER_STATE_G3);
	zassert_equal(0, power_signal_get(PWR_EN_PP3300_A));
	zassert_equal(0, power_signal_get(PWR_EN_PP5000_A));
	zassert_equal(0, power_signal_get(PWR_DSW_PWROK));
}

/*
 * @brief TestPurpose: Check G3 power handler response.
 *
 * @details
 * G3 action handler set power signals when `AP_PWRSEQ_EVENT_POWER_STARTUP`
 * event is posted.
 *
 * Expected Results:
 * - Current power state is G3.
 * - G3 action handler sets power signals properly when event
 * `AP_PWRSEQ_EVENT_POWER_STARTUP` is posted.
 */
ZTEST(nissa_board_power, test_board_ap_power_g3_run_2)
{
	const struct device *dev = ap_pwrseq_get_instance();

	zassert_equal(0,
		      power_signal_emul_load(EMUL_POWER_SIGNAL_TEST_PLATFORM(
			      tp_power_up_fail)),
		      "Unable to load test platform `tp_power_up_fail`");

	zassert_equal(ap_pwrseq_get_current_state(dev), AP_POWER_STATE_G3);
	zassert_equal(0, power_signal_get(PWR_EN_PP3300_A));
	zassert_equal(0, power_signal_get(PWR_EN_PP5000_A));
	zassert_equal(0, power_signal_get(PWR_DSW_PWROK));
	ap_pwrseq_post_event(dev, AP_PWRSEQ_EVENT_POWER_STARTUP);
	zassert_equal(1, power_signal_get(PWR_EN_PP3300_A));
	zassert_equal(1, power_signal_get(PWR_EN_PP5000_A));
	zassert_equal(0, power_signal_get(PWR_DSW_PWROK));
	zassert_equal(ap_pwrseq_get_current_state(dev), AP_POWER_STATE_G3);
}

/*
 * @brief TestPurpose: Check G3 power handler response.
 *
 * @details
 * G3 action handler set power signals when `AP_PWRSEQ_EVENT_POWER_STARTUP`
 * event is posted.
 *
 * Expected Results:
 * - Current power state is G3.
 * - G3 action handler sets power signals properly when event
 * `AP_PWRSEQ_EVENT_POWER_STARTUP` is posted.
 * - G3 action handler verifies power signals are set.
 */
ZTEST(nissa_board_power, test_board_ap_power_g3_run_3)
{
	const struct device *dev = ap_pwrseq_get_instance();

	zassert_equal(0,
		      power_signal_emul_load(
			      EMUL_POWER_SIGNAL_TEST_PLATFORM(tp_power_up_ok)),
		      "Unable to load test platform `tp_power_up_ok`");

	zassert_equal(ap_pwrseq_get_current_state(dev), AP_POWER_STATE_G3);
	zassert_equal(0, power_signal_get(PWR_EN_PP3300_A));
	zassert_equal(0, power_signal_get(PWR_DSW_PWROK));
	ap_pwrseq_post_event(dev, AP_PWRSEQ_EVENT_POWER_STARTUP);
	zassert_equal(1, power_signal_get(PWR_EN_PP3300_A));
	zassert_equal(1, power_signal_get(PWR_DSW_PWROK));
	zassert_equal(ap_pwrseq_get_current_state(dev), AP_POWER_STATE_S3);
}

/*
 * @brief TestPurpose: Check S0 power handler response.
 *
 * @details
 * Since S0 action handler for Nissa does notdo any power signal check, this
 * test will only check G3 entry.
 *
 * Expected Results:
 * - Current power state is S3.
 * - S0 action handler is called.
 * - G3 action handler entry sets power signals properly when event
 * `AP_PWRSEQ_EVENT_POWER_SHUTDOWN` is posted.
 * - G3 action handler verifies power signals are set.
 */
ZTEST(nissa_board_power, test_board_ap_power_s0_run_0)
{
	const struct device *dev = ap_pwrseq_get_instance();

	zassert_equal(0,
		      power_signal_emul_load(
			      EMUL_POWER_SIGNAL_TEST_PLATFORM(tp_s0_to_g3)),
		      "Unable to load test platform `tp_s0_to_s3`");

	zassert_equal(ap_pwrseq_get_current_state(dev), AP_POWER_STATE_S3);
	ap_pwrseq_post_event(dev, AP_PWRSEQ_EVENT_POWER_SIGNAL);
	zassert_equal(ap_pwrseq_get_current_state(dev), AP_POWER_STATE_S0);
	zassert_equal(chipset_s0_run_count, 1);

	zassert_equal(0, power_signal_get(PWR_SLP_SUS));
	zassert_equal(1, power_signal_get(PWR_RSMRST_PWRGD));
	zassert_equal(1, power_signal_get(PWR_DSW_PWROK));
	ap_pwrseq_post_event(dev, AP_PWRSEQ_EVENT_POWER_SHUTDOWN);
	zassert_equal(ap_pwrseq_get_current_state(dev), AP_POWER_STATE_G3);
	zassert_equal(chipset_s0_run_count, 2);

	zassert_equal(1, power_signal_get(PWR_SLP_SUS));
	zassert_equal(0, power_signal_get(PWR_RSMRST_PWRGD));
	zassert_equal(0, power_signal_get(PWR_DSW_PWROK));
}

/**
 * Supporting functions for test
 **/
static int chipset_ap_power_s0_run(void *data)
{
	chipset_s0_run_count++;
	if (ap_pwrseq_sm_is_event_set(data, AP_PWRSEQ_EVENT_POWER_SHUTDOWN)) {
		return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_G3);
	}
	return 0;
}

AP_POWER_CHIPSET_STATE_DEFINE(AP_POWER_STATE_S0, NULL, chipset_ap_power_s0_run,
			      NULL);

static int chipset_ap_power_s3_run(void *data)
{
	if (ap_pwrseq_sm_is_event_set(data, AP_PWRSEQ_EVENT_POWER_SIGNAL)) {
		return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_S0);
	}

	return 0;
}

AP_POWER_CHIPSET_STATE_DEFINE(AP_POWER_STATE_S3, NULL, chipset_ap_power_s3_run,
			      NULL);

static int chipset_ap_power_g3_run(void *data)
{
	return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_S3);
}

AP_POWER_CHIPSET_STATE_DEFINE(AP_POWER_STATE_G3, NULL, chipset_ap_power_g3_run,
			      NULL);

static int x86_non_dsx_adlp_s0ix_run(void *data)
{
	return 0;
}

AP_POWER_CHIPSET_SUB_STATE_DEFINE(AP_POWER_STATE_S0IX, NULL,
				  x86_non_dsx_adlp_s0ix_run, NULL,
				  AP_POWER_STATE_S0);
