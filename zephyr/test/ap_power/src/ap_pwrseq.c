/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power.h"
#include "ap_power/ap_power_interface.h"
#include "chipset.h"
#include "ec_commands.h"
#include "emul/emul_power_signals.h"
#include "host_command.h"
#include "lpc.h"
#include "power_signals.h"
#include "test_mocks.h"
#include "test_state.h"
#include "zephyr/sys/util.h"

#include <zephyr/drivers/espi.h>
#include <zephyr/drivers/espi_emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <ap_power/ap_pwrseq.h>
#include <ap_power_override_functions.h>

LOG_MODULE_REGISTER(test_ap_pwrseq);

static struct ap_power_ev_callback test_cb;
static int power_resume_count;
static int power_start_up_count;
static int power_hard_off_count;
static int power_shutdown_count;
static int power_shutdown_complete_count;
static int power_suspend_count;

#define S5_INACTIVITY_TIMEOUT_MS                                               \
	COND_CODE_0(                                                           \
		AP_PWRSEQ_DT_VALUE(s5_inactivity_timeout), (2 * MSEC_PER_SEC), \
		(AP_PWRSEQ_DT_VALUE(s5_inactivity_timeout) * MSEC_PER_SEC))

#define TEST_SLEEP_TIMEOUT_MS 50

#ifdef CONFIG_AP_PWRSEQ_DRIVER
static void ap_pwrseq_wake(void)
{
	const struct device *dev = ap_pwrseq_get_instance();

	ap_pwrseq_post_event(dev, AP_PWRSEQ_EVENT_POWER_SIGNAL);
}
#endif

#define PWRSEQ_OUTPUT(nodeid)                                 \
	{                                                     \
		.signal_enum = PWR_SIGNAL_ENUM(nodeid),       \
		.signal_name = DT_PROP(nodeid, enum_name),    \
		.gpio_spec = GPIO_DT_SPEC_GET(nodeid, gpios), \
	},

#define PWRSEQ_GPIOS(nodeid) \
	COND_CODE_1(DT_PROP(nodeid, output), (PWRSEQ_OUTPUT(nodeid)), ())

struct ec_output {
	uint8_t signal_enum;
	char *signal_name;
	struct gpio_dt_spec gpio_spec;
};

/*
 * Get a list of power signals that are GPIO outputs.
 */
static const struct ec_output ec_outputs[] = { DT_FOREACH_STATUS_OKAY(
	intel_ap_pwrseq_gpio, PWRSEQ_GPIOS) };

/*
 * List of input signals to the AP that are driven by the EC. All signals
 * should start out at physical level 0 while in G3 and should end up at
 * physical level 1 when reaching S0.
 */
#ifdef CONFIG_AP_X86_INTEL_MTL
static const uint8_t ap_inputs[] = {
	PWR_EC_PCH_RSMRST,
	PWR_EC_PCH_SYS_PWROK,
};
#endif

#ifdef CONFIG_AP_X86_INTEL_ADL
static const uint8_t ap_inputs[] = {
	PWR_EC_SOC_DSW_PWROK,
	PWR_PCH_PWROK,
	PWR_EC_PCH_RSMRST,
#ifndef CONFIG_AP_PWRSEQ_DRIVER
	/* TODO: b/317918383 - AP_PWRSEQ_DRIVER: ADL chipset needs to support
	 * PWR_VCCST_PWRGD and PWR_EC_PCH_SYS_PWROK
	 */
	PWR_VCCST_PWRGD,
	PWR_EC_PCH_SYS_PWROK,
#endif
};
#endif

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

static void ap_pwrseq_reset_ev_counters(void)
{
	power_resume_count = 0;
	power_start_up_count = 0;
	power_hard_off_count = 0;
	power_shutdown_count = 0;
	power_shutdown_complete_count = 0;
	power_suspend_count = 0;
}

static void verify_ap_inputs(bool in_s0)
{
	int expected_level;

	if (in_s0) {
		expected_level = 1;
	} else {
		expected_level = 0;
	}

	LOG_INF("Verifying AP inputs are at physical level %d", expected_level);

	for (int ap = 0; ap < ARRAY_SIZE(ap_inputs); ap++) {
		for (int ec = 0; ec < ARRAY_SIZE(ec_outputs); ec++) {
			int phys_level;

			if (ec_outputs[ec].signal_enum == ap_inputs[ap]) {
				phys_level = gpio_emul_output_get(
					ec_outputs[ec].gpio_spec.port,
					ec_outputs[ec].gpio_spec.pin);
				zassert_equal(
					phys_level, expected_level,
					"%s (%d) signal isn't at physical %d",
					ec_outputs[ec].signal_name,
					ec_outputs[ec].signal_enum,
					expected_level);
			}
		}
	}
}

ZTEST(ap_pwrseq, test_ap_pwrseq_0)
{
	/* Verify all inputs to the AP start a physical level 0. */
	verify_ap_inputs(false);

	zassert_equal(0,
		      power_signal_emul_load(
			      EMUL_POWER_SIGNAL_TEST_PLATFORM(tp_sys_g3_to_s0)),
		      "Unable to load test platform `tp_sys_g3_to_s0`");

	k_msleep(500);

	zassert_equal(1, power_start_up_count,
		      "AP_POWER_STARTUP event not generated");
	zassert_equal(1, power_resume_count,
		      "AP_POWER_RESUME event not generated");

	/* Once reaching S0, validate that all inputs to the
	 * AP are set to high level.
	 */
	verify_ap_inputs(true);
}

/* Sleep hang test - this assumes the test is run after the test_ap_pwrseq_0
 * test above and that the current power state is S0.
 * At completion the power state remains in S0.
 */
ZTEST(ap_pwrseq, test_ap_pwrseq_0_sleep_hang)
{
	host_event_t lpc_event_mask;
	host_event_t mask = EC_HOST_EVENT_MASK(EC_HOST_EVENT_HANG_DETECT);

	/* Enable the hang detect event in the LPC event mask. */
	lpc_event_mask = lpc_get_host_event_mask(LPC_HOST_EVENT_SCI);
	lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, lpc_event_mask | mask);

	struct ec_params_host_sleep_event_v1 host_sleep_ev_p = {
		.sleep_event = HOST_SLEEP_EVENT_S0IX_SUSPEND,
		.suspend_params.sleep_timeout_ms = TEST_SLEEP_TIMEOUT_MS,
	};
	struct ec_response_host_sleep_event_v1 host_sleep_ev_r;
	struct host_cmd_handler_args host_sleep_ev_args = BUILD_HOST_COMMAND(
		EC_CMD_HOST_SLEEP_EVENT, 1, host_sleep_ev_r, host_sleep_ev_p);

	/* Now tell the EC that the AP is going to sleep, but don't change
	 * any of the power signals.  This causes a sleep timeout.
	 */
	zassert_ok(host_command_process(&host_sleep_ev_args));

	/* Purposely leave the SLP_S0 signal de-asserted to cause a timeout. */
	k_msleep(TEST_SLEEP_TIMEOUT_MS * 2);

	zassert_true(host_is_event_set(EC_HOST_EVENT_HANG_DETECT));

	/* Retest, but this time set an infinite timeout.  Verify no event
	 * is reported.
	 */
	host_clear_events(EC_HOST_EVENT_MASK(EC_HOST_EVENT_HANG_DETECT));
	host_sleep_ev_p.suspend_params.sleep_timeout_ms =
		EC_HOST_SLEEP_TIMEOUT_INFINITE;
	zassert_ok(host_command_process(&host_sleep_ev_args));
	k_sleep(K_SECONDS(10));

	zassert_false(host_is_event_set(EC_HOST_EVENT_HANG_DETECT));
}

/* Sleep success test - this assumes the current power state is S0 and
 * at completion the power state will be S0ix.
 */
ZTEST(ap_pwrseq, test_ap_pwrseq_0_sleep_success)
{
	struct ec_params_host_sleep_event_v1 host_sleep_ev_p = {
		.sleep_event = HOST_SLEEP_EVENT_S0IX_SUSPEND,
		.suspend_params = { EC_HOST_SLEEP_TIMEOUT_DEFAULT },
	};
	struct ec_response_host_sleep_event_v1 host_sleep_ev_r;
	struct host_cmd_handler_args host_sleep_ev_args = BUILD_HOST_COMMAND(
		EC_CMD_HOST_SLEEP_EVENT, 1, host_sleep_ev_r, host_sleep_ev_p);

	struct ec_params_s0ix_cnt s0ix_cnt_ev_p = {
		.flags = EC_S0IX_COUNTER_RESET
	};
	struct ec_response_s0ix_cnt s0ix_cnt_ev_r;
	struct host_cmd_handler_args s0ix_cnt_ev_args = BUILD_HOST_COMMAND(
		EC_CMD_GET_S0IX_COUNTER, 0, s0ix_cnt_ev_r, s0ix_cnt_ev_p);

	/* Verify that counter is set to 0 */
	zassert_ok(host_command_process(&s0ix_cnt_ev_args),
		   "Failed to get s0ix counter");
	zassert_equal(s0ix_cnt_ev_r.s0ix_counter, 0);

	/* Send host sleep event */
	zassert_ok(host_command_process(&host_sleep_ev_args));

	/* Assert SLP_S0# */
	zassert_equal(0,
		      power_signal_emul_load(
			      EMUL_POWER_SIGNAL_TEST_PLATFORM(tp_sys_sleep)),
		      "Unable to load test platform `tp_sys_g3_to_s0`");

	k_msleep(500);

	/*
	 * Verify that counter has been increased,
	 * clear the flag for get command
	 */
	s0ix_cnt_ev_p.flags = 0;
	zassert_ok(host_command_process(&s0ix_cnt_ev_args),
		   "Failed to get s0ix counter");
	zassert_equal(s0ix_cnt_ev_r.s0ix_counter, 1);
}

/* Wake from S0ix.  This test assumes the current power state is S0ix and
 * at completion the power state is S0.
 */
ZTEST(ap_pwrseq, test_ap_pwrseq_0_wake)
{
	struct ec_params_host_sleep_event_v1 p = {
		.sleep_event = HOST_SLEEP_EVENT_S0IX_RESUME,
		.suspend_params = { EC_HOST_SLEEP_TIMEOUT_DEFAULT },
	};
	struct ec_response_host_sleep_event_v1 r;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_HOST_SLEEP_EVENT, 1, r, p);
	struct ec_params_s0ix_cnt s0ix_cnt_ev_p = { .flags = 0 };
	struct ec_response_s0ix_cnt s0ix_cnt_ev_r;
	struct host_cmd_handler_args s0ix_cnt_ev_args = BUILD_HOST_COMMAND(
		EC_CMD_GET_S0IX_COUNTER, 0, s0ix_cnt_ev_r, s0ix_cnt_ev_p);

	/* Confirm that counters keeps the same value through wakeup */
	zassert_ok(host_command_process(&s0ix_cnt_ev_args),
		   "Failed to get s0ix counter");
	zassert_equal(s0ix_cnt_ev_r.s0ix_counter, 1);

	zassert_equal(0,
		      power_signal_emul_load(
			      EMUL_POWER_SIGNAL_TEST_PLATFORM(tp_sys_wake)),
		      "Unable to load test platform `tp_sys_g3_to_s0`");

	k_msleep(500);
	zassert_ok(host_command_process(&args));
	zassert_ok(host_command_process(&s0ix_cnt_ev_args),
		   "Failed to get sleep counter");
	zassert_equal(s0ix_cnt_ev_r.s0ix_counter, 1);

	/* Verify the reset command sets the counter to zero */
	s0ix_cnt_ev_p.flags = EC_S0IX_COUNTER_RESET;
	zassert_ok(host_command_process(&s0ix_cnt_ev_args),
		   "Failed to get s0ix counter");

	s0ix_cnt_ev_p.flags = 0;
	zassert_ok(host_command_process(&s0ix_cnt_ev_args),
		   "Failed to get s0ix counter");
	zassert_equal(s0ix_cnt_ev_r.s0ix_counter, 0);
}

ZTEST(ap_pwrseq, test_ap_pwrseq_1)
{
	zassert_equal(0,
		      power_signal_emul_load(EMUL_POWER_SIGNAL_TEST_PLATFORM(
			      tp_sys_s0_power_fail)),
		      "Unable to load test platform `tp_sys_s0_power_fail`");

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
		"Unable to load test platform `tp_sys_g3_to_s0_power_down`");

	ap_power_exit_hardoff();
	k_sleep(K_MSEC(S5_INACTIVITY_TIMEOUT_MS * 1.5));
	zassert_equal(1, power_shutdown_count,
		      "AP_POWER_SHUTDOWN event not generated");
	zassert_equal(1, power_shutdown_complete_count,
		      "AP_POWER_SHUTDOWN_COMPLETE event not generated");
	zassert_equal(1, power_suspend_count,
		      "AP_POWER_SUSPEND event not generated");
	zassert_equal(1, power_hard_off_count,
		      "AP_POWER_HARD_OFF event not generated");
}

#if defined(CONFIG_AP_X86_INTEL_ADL)
ZTEST(ap_pwrseq, test_ap_pwrseq_3)
{
	zassert_equal(0,
		      power_signal_emul_load(EMUL_POWER_SIGNAL_TEST_PLATFORM(
			      tp_sys_s5_slp_sus_fail)),
		      "Unable to load test platform `tp_sys_s5_slp_sus_fail`");

	ap_power_exit_hardoff();
	k_msleep(500);

	/*
	 * AP_PWRSEQ_DRIVER inhibits transition up from G3 due to slp_sus signal
	 * error, whereas the other implementation goes to G3S5 then notices the
	 * problem and goes back to G3, emitting a AP_POWER_HARD_OFF event in
	 * the process.
	 */
	if (IS_ENABLED(CONFIG_AP_PWRSEQ_DRIVER)) {
		zassert_equal(0, power_hard_off_count,
			      "AP_POWER_HARD_OFF event generated");
	} else {
		zassert_equal(1, power_hard_off_count,
			      "AP_POWER_HARD_OFF event not generated");
	}
}

ZTEST(ap_pwrseq, test_ap_pwrseq_4)
{
	zassert_equal(
		0,
		power_signal_emul_load(EMUL_POWER_SIGNAL_TEST_PLATFORM(
			tp_sys_s4_dsw_pwrok_fail)),
		"Unable to load test platform `tp_sys_s4_dsw_pwrok_fail`");

	ap_power_exit_hardoff();
	k_msleep(500);

	zassert_equal(0, power_hard_off_count,
		      "AP_POWER_HARD_OFF event generated");
	zassert_equal(1, power_shutdown_count,
		      "AP_POWER_SHUTDOWN event not generated");
	zassert_equal(1, power_shutdown_complete_count,
		      "AP_POWER_SHUTDOWN_COMPLETE event not generated");
}

ZTEST(ap_pwrseq, test_ap_pwrseq_5)
{
	zassert_equal(
		0,
		power_signal_emul_load(EMUL_POWER_SIGNAL_TEST_PLATFORM(
			tp_sys_s3_dsw_pwrok_fail)),
		"Unable to load test platform `tp_sys_s3_dsw_pwrok_fail`");

	ap_power_exit_hardoff();
	k_msleep(500);

	zassert_equal(0, power_hard_off_count,
		      "AP_POWER_HARD_OFF event generated");
	zassert_equal(1, power_shutdown_count,
		      "AP_POWER_SHUTDOWN event not generated");
	zassert_equal(1, power_shutdown_complete_count,
		      "AP_POWER_SHUTDOWN_COMPLETE event not generated");
}
#endif /* CONFIG_AP_X86_INTEL_ADL */

ZTEST(ap_pwrseq, test_ap_pwrseq_6)
{
	zassert_equal(
		0,
		power_signal_emul_load(
			EMUL_POWER_SIGNAL_TEST_PLATFORM(tp_sys_s3_rsmrst_fail)),
		"Unable to load test platform `tp_sys_s3_dsw_pwrok_fail`");

	ap_power_exit_hardoff();
	k_sleep(K_MSEC(S5_INACTIVITY_TIMEOUT_MS * 1.5));

#if defined(CONFIG_AP_X86_INTEL_ADL)
	zassert_equal(1, power_hard_off_count,
		      "AP_POWER_HARD_OFF event generated");
#endif /* CONFIG_AP_X86_INTEL_ADL */
	zassert_equal(1, power_start_up_count,
		      "AP_POWER_STARTUP event not generated");
	zassert_equal(1, power_shutdown_count,
		      "AP_POWER_SHUTDOWN event not generated");
	zassert_equal(1, power_shutdown_complete_count,
		      "AP_POWER_SHUTDOWN_COMPLETE event not generated");
}

ZTEST(ap_pwrseq, test_insufficient_power_blocks_s5)
{
	zassert_equal(0,
		      power_signal_emul_load(
			      EMUL_POWER_SIGNAL_TEST_PLATFORM(tp_sys_g3_to_s0)),
		      "Unable to load test platform `tp_sys_g3_to_s0`");
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
	ap_pwrseq_reset_ev_counters();
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
