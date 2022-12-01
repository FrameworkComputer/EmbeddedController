/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power.h"
#include "ap_power/ap_power_interface.h"
#include "chipset.h"
#include "emul/emul_power_signals.h"
#include "test_mocks.h"
#include "test_state.h"

#include <zephyr/drivers/espi.h>
#include <zephyr/drivers/espi_emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <ap_power/ap_pwrseq.h>

static struct ap_power_ev_callback test_cb;
static int power_resume_count;
static int power_start_up_count;
static int power_hard_off_count;
static int power_shutdown_count;
static int power_shutdown_complete_count;
static int power_suspend_count;

static void emul_ev_handler(struct ap_power_ev_callback *callback,
			    struct ap_power_ev_data data)
{
	switch (data.event) {
	case AP_POWER_RESUME:
		power_resume_count++;
		break;

	case AP_POWER_STARTUP:
		power_start_up_count++;
		break;

	case AP_POWER_HARD_OFF:
		power_hard_off_count++;
		break;

	case AP_POWER_SHUTDOWN:
		power_shutdown_count++;
		break;

	case AP_POWER_SHUTDOWN_COMPLETE:
		power_shutdown_complete_count++;
		break;

	case AP_POWER_SUSPEND:
		power_suspend_count++;
		break;
	default:
		break;
	};
}

ZTEST(ap_pwrseq, test_ap_pwrseq_0)
{
	zassert_equal(0,
		      power_signal_emul_load(
			      EMUL_POWER_SIGNAL_TEST_PLATFORM(tp_sys_g3_to_s0)),
		      "Unable to load test platfform `tp_sys_g3_to_s0`");

	k_msleep(500);

	zassert_equal(1, power_start_up_count,
		      "AP_POWER_STARTUP event not generated");
	zassert_equal(1, power_resume_count,
		      "AP_POWER_RESUME event not generated");
}

ZTEST(ap_pwrseq, test_ap_pwrseq_1)
{
	zassert_equal(0,
		      power_signal_emul_load(EMUL_POWER_SIGNAL_TEST_PLATFORM(
			      tp_sys_s0_power_fail)),
		      "Unable to load test platfform `tp_sys_s0_power_fail`");

	/*
	 * Once emulated power signals are loaded, we need to wake AP power
	 * Sequence thread up to start executing new set of power signals
	 */
	ap_pwrseq_wake();
	k_msleep(500);
	zassert_equal(1, power_shutdown_count,
		      "AP_POWER_SHUTDOWN event not generated");
	zassert_equal(1, power_shutdown_complete_count,
		      "AP_POWER_SHUTDOWN_COMPLETE event not generated");
	zassert_equal(0, power_suspend_count,
		      "AP_POWER_SUSPEND event generated");
}

ZTEST(ap_pwrseq, test_ap_pwrseq_2)
{
	zassert_equal(
		0,
		power_signal_emul_load(EMUL_POWER_SIGNAL_TEST_PLATFORM(
			tp_sys_g3_to_s0_power_down)),
		"Unable to load test platfform `tp_sys_g3_to_s0_power_down`");

	ap_power_exit_hardoff();
	k_msleep(2000);
	zassert_equal(3, power_shutdown_count,
		      "AP_POWER_SHUTDOWN event not generated");
	zassert_equal(3, power_shutdown_complete_count,
		      "AP_POWER_SHUTDOWN_COMPLETE event not generated");
	zassert_equal(1, power_suspend_count,
		      "AP_POWER_SUSPEND event generated");
	zassert_equal(1, power_hard_off_count,
		      "AP_POWER_HARD_OFF event generated");
}

ZTEST(ap_pwrseq, test_insufficient_power_blocks_s5)
{
	zassert_equal(0,
		      power_signal_emul_load(
			      EMUL_POWER_SIGNAL_TEST_PLATFORM(tp_sys_g3_to_s0)),
		      "Unable to load test platfform `tp_sys_g3_to_s0`");
	system_can_boot_ap_fake.return_val = 0;

	ap_power_exit_hardoff();
	k_msleep(5000);

	zassert_equal(40, system_can_boot_ap_fake.call_count);
	zassert_true(
		chipset_in_or_transitioning_to_state(CHIPSET_STATE_HARD_OFF));
}

void ap_pwrseq_after_test(void *data)
{
	power_signal_emul_unload();
}

void *ap_pwrseq_setup_suite(void)
{
	ap_power_ev_init_callback(&test_cb, emul_ev_handler,
				  AP_POWER_RESUME | AP_POWER_STARTUP |
					  AP_POWER_HARD_OFF | AP_POWER_SUSPEND |
					  AP_POWER_SHUTDOWN |
					  AP_POWER_SHUTDOWN_COMPLETE);

	ap_power_ev_add_callback(&test_cb);

	return NULL;
}

void ap_pwrseq_teardown_suite(void *data)
{
	ap_power_ev_remove_callback(&test_cb);
}

ZTEST_SUITE(ap_pwrseq, ap_power_predicate_post_main, ap_pwrseq_setup_suite,
	    NULL, ap_pwrseq_after_test, NULL);
