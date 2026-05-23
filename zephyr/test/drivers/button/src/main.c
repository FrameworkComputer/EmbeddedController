/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "base_state.h"
#include "button.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "include/power_button.h"
#include "mkbp_fifo.h"
#include "power.h"
#include "test/drivers/test_state.h"
#include "timer.h"

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

/*
 * TODO (b/b/253284635) Timeouts here don't quite align with the button press
 *   duration. This is caused by an issue with the Zephyr scheduling for delayed
 *   work that's causing us to need to sleep longer than "reasonable".
 */

FAKE_VOID_FUNC(chipset_reset, enum chipset_shutdown_reason);
FAKE_VOID_FUNC(base_force_state, enum ec_set_base_state_cmd);

static char *button_debug_state_strings[] = {
	"STATE_DEBUG_NONE", "STATE_DEBUG_CHECK",
	"STATE_STAGING",    "STATE_DEBUG_MODE_ACTIVE",
	"STATE_SYSRQ_PATH", "STATE_WARM_RESET_PATH",
	"STATE_SYSRQ_EXEC", "STATE_WARM_RESET_EXEC",
};

#define ASSERT_DEBUG_STATE(expected)                                          \
	do {                                                                  \
		enum debug_state state = get_button_debug_state();            \
		zassert_equal(expected, state,                                \
			      "Button debug state expected to be %d(%s),"     \
			      " but was %d(%s)",                              \
			      expected, button_debug_state_strings[expected], \
			      state, button_debug_state_strings[state]);      \
	} while (false)

struct button_fixture {
	timestamp_t fake_time;
};

static void *button_setup(void)
{
	static struct button_fixture fixture;

	/* Set the mock clock */
	get_time_mock = &fixture.fake_time;
	return &fixture;
}

static void button_before(void *f)
{
	((struct button_fixture *)f)->fake_time.val = 0;
	reset_button_debug_state();
	button_init();
	/* Sleep for 30s to flush any pending tasks */
	k_sleep(K_SECONDS(30));
	mkbp_clear_fifo();

	RESET_FAKE(chipset_reset);
}

ZTEST_SUITE(button, drivers_predicate_post_main, button_setup, button_before,
	    NULL, NULL);

static inline void pass_time(uint64_t duration_ms)
{
	for (uint64_t i = 0; i <= duration_ms; i += 100) {
		get_time_mock->val += 100 * USEC_PER_MSEC;
		k_msleep(100);
	}
}

ZTEST(button, test_press_one_button_no_change)
{
	/* Press the volume-up button for 1/2 a second */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vup 500"));

	/* Wait for the timeout */
	pass_time(11000);
	ASSERT_DEBUG_STATE(STATE_DEBUG_NONE);
}

ZTEST(button, test_press_vup_vdown_too_short)
{
	/* Press both volume-up and volume-down for 1/2 second */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vup 500"));
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vdown 500"));

	/* Let the deferred calls get run (300ms) */
	pass_time(300);
	ASSERT_DEBUG_STATE(STATE_DEBUG_CHECK);

	/* Wait for the timeout */
	pass_time(11000);
	ASSERT_DEBUG_STATE(STATE_DEBUG_NONE);
}

ZTEST(button, test_fail_check_button_released_too_soon)
{
	/* Press both volume-up and volume-down for 9 seconds */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vup 9000"));
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vdown 9000"));

	/* Let the deferred calls get run (800ms) */
	pass_time(800);
	ASSERT_DEBUG_STATE(STATE_DEBUG_CHECK);

	/* Wait for the buttons to be released */
	pass_time(9300);
	ASSERT_DEBUG_STATE(STATE_DEBUG_NONE);
}

ZTEST(button, test_fail_check_button_stuck)
{
	/* Press both volume-up and volume-down for 0.9 seconds */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vup 30000"));
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vdown 30000"));

	/* Let the deferred calls get run (800ms) */
	pass_time(800);
	ASSERT_DEBUG_STATE(STATE_DEBUG_CHECK);

	/* Wait for the timeout, should put us in staging */
	pass_time(11000);
	ASSERT_DEBUG_STATE(STATE_STAGING);

	/* Do a plain sleep to force the error condition of waking up the
	 * handler too early (since the time isn't moving forward).
	 */
	k_msleep(11000);

	/* Now sleep and move the clock forward to timeout the debug process */
	pass_time(21000);
	ASSERT_DEBUG_STATE(STATE_DEBUG_NONE);
}

#ifdef CONFIG_DETACHABLE_BASE
static inline bool get_sysrq_led_status(void)
{
	return gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_chg_led_y_c1));
}

ZTEST(button, test_activate_sysrq_led_flickering)
{
	bool is_sysrq_active;
	/*
	 * Issue press both volume-up and volume-down for 10.5 seconds to in EC
	 * debug mode.
	 */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vup 10500"));
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vdown 10500"));

	/* Let the deferred calls get run (800ms) */
	pass_time(800);
	/* Jump after button debounce time passed, and is in debug checking */
	pass_time(500);
	/* Jump for simulated button request for releasing */
	pass_time(10000);
	/* Jump for button debounce time passed */
	pass_time(500);
	ASSERT_DEBUG_STATE(STATE_DEBUG_MODE_ACTIVE);

	/*
	 * LED flickering is running in tick hook, so just sleep the thread, and
	 * query the pin status every HOOK_TICK_INTERVAL_MS.
	 */
	is_sysrq_active = get_sysrq_led_status();
	k_msleep(HOOK_TICK_INTERVAL_MS);
	zassert_not_equal(is_sysrq_active, get_sysrq_led_status());
	k_msleep(HOOK_TICK_INTERVAL_MS);
	zassert_equal(is_sysrq_active, get_sysrq_led_status());

	/* Now sleep and move the clock forward to timeout the debug process */
	pass_time(11000);
}
#endif /* CONFIG_DETACHABLE_BASE */

ZTEST(button, test_activate_sysrq_path_then_timeout)
{
	/* Press both volume-up and volume-down for 1/2 second */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vup 10500"));
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vdown 10500"));

	/* Let the deferred calls get run (800ms) */
	pass_time(800);
	ASSERT_DEBUG_STATE(STATE_DEBUG_CHECK);

	/* Wait for total 10 seconds */
	pass_time(9400);
	ASSERT_DEBUG_STATE(STATE_STAGING);

	/* Wait for the buttons to be released and check that we activated debug
	 * mode */
	pass_time(1000);
	ASSERT_DEBUG_STATE(STATE_DEBUG_MODE_ACTIVE);

	/* Press volume up button to put in sysrq_path */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vup 500"));
	pass_time(200);
	ASSERT_DEBUG_STATE(STATE_STAGING);

	/* Wait for timeout and go into sysrq_path */
	pass_time(500);
	ASSERT_DEBUG_STATE(STATE_SYSRQ_PATH);

	/* Now sleep and move the clock forward to timeout the debug process */
	pass_time(11000);
	ASSERT_DEBUG_STATE(STATE_DEBUG_NONE);
}

ZTEST(button, test_activate_sysrq_path_4_times)
{
	/* Press both volume-up and volume-down for 1/2 second */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vup 10500"));
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vdown 10500"));

	/* Let the deferred calls get run (800ms) */
	pass_time(800);
	ASSERT_DEBUG_STATE(STATE_DEBUG_CHECK);

	/* Wait for total 10 seconds */
	pass_time(9400);
	ASSERT_DEBUG_STATE(STATE_STAGING);

	/* Wait for the buttons to be released and check that we activated debug
	 * mode */
	pass_time(1000);
	ASSERT_DEBUG_STATE(STATE_DEBUG_MODE_ACTIVE);

	/* Press volume up button to put in sysrq_path */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vup 500"));
	pass_time(200);
	ASSERT_DEBUG_STATE(STATE_STAGING);

	/* Wait for timeout and go into sysrq_path */
	pass_time(500);
	ASSERT_DEBUG_STATE(STATE_SYSRQ_PATH);

	/* Press vup again (#2) */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vup 500"));
	pass_time(1300);

	/* Press vup again (#3) */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vup 500"));
	pass_time(1300);

	/* Press vup again (#4) */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vup 500"));
	pass_time(1300);
	ASSERT_DEBUG_STATE(STATE_DEBUG_NONE);
}

ZTEST(button, test_activate_sysrq_exec)
{
	uint32_t event_data = 0;

	/* Press both volume-up and volume-down for 10.5 second */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vup 10500"));
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vdown 10500"));

	/* Let the deferred calls get run (800ms) */
	pass_time(800);
	ASSERT_DEBUG_STATE(STATE_DEBUG_CHECK);

	/* Wait for total 10 seconds */
	pass_time(9400);
	ASSERT_DEBUG_STATE(STATE_STAGING);

	/* Wait for the buttons to be released and check that we activated debug
	 * mode */
	pass_time(1000);
	ASSERT_DEBUG_STATE(STATE_DEBUG_MODE_ACTIVE);

	/* Press volume up button to put in sysrq_path */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vup 500"));
	pass_time(200);
	ASSERT_DEBUG_STATE(STATE_STAGING);

	/* Wait for timeout and go into sysrq_path */
	pass_time(500);
	ASSERT_DEBUG_STATE(STATE_SYSRQ_PATH);

	/* Now sleep and move the clock forward to timeout the debug process */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vdown 500"));
	pass_time(800);
	pass_time(500);
	ASSERT_DEBUG_STATE(STATE_DEBUG_NONE);

	/* Flush all the button events */
	while (mkbp_fifo_get_next_event((uint8_t *)&event_data,
					EC_MKBP_EVENT_BUTTON) > 0)
		;

	/* Check for sysrq event */
	zassert_equal(4, mkbp_fifo_get_next_event((uint8_t *)&event_data,
						  EC_MKBP_EVENT_SYSRQ));
	zassert_equal((uint32_t)'x', event_data);
}

ZTEST(button, test_activate_warm_reset_then_timeout)
{
	/* Press both volume-up and volume-down for 1/2 second */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vup 10500"));
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vdown 10500"));

	/* Let the deferred calls get run (800ms) */
	pass_time(800);
	ASSERT_DEBUG_STATE(STATE_DEBUG_CHECK);

	/* Wait for total 10 seconds */
	pass_time(9400);
	ASSERT_DEBUG_STATE(STATE_STAGING);

	/* Wait for the buttons to be released and check that we activated debug
	 * mode */
	pass_time(1000);
	ASSERT_DEBUG_STATE(STATE_DEBUG_MODE_ACTIVE);

	/* Press volume down button to put in warm_reset_path */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vdown 500"));
	pass_time(200);
	ASSERT_DEBUG_STATE(STATE_STAGING);

	/* Wait for timeout and go into warm_reset_path */
	pass_time(500);
	ASSERT_DEBUG_STATE(STATE_WARM_RESET_PATH);

	/* Now sleep and move the clock forward to timeout the debug process */
	pass_time(11000);
	ASSERT_DEBUG_STATE(STATE_DEBUG_NONE);
}

ZTEST(button, test_activate_warm_reset_exec)
{
	/* Press both volume-up and volume-down for 1/2 second */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vup 10500"));
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vdown 10500"));

	/* Let the deferred calls get run (800ms) */
	pass_time(800);
	ASSERT_DEBUG_STATE(STATE_DEBUG_CHECK);

	/* Wait for total 10 seconds */
	pass_time(9400);
	ASSERT_DEBUG_STATE(STATE_STAGING);

	/* Wait for the buttons to be released and check that we activated debug
	 * mode */
	pass_time(1000);
	ASSERT_DEBUG_STATE(STATE_DEBUG_MODE_ACTIVE);

	/* Press volume down button to put in warm_reset_path */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vdown 500"));
	pass_time(200);
	ASSERT_DEBUG_STATE(STATE_STAGING);

	/* Wait for timeout and go into warm_reset_path */
	pass_time(500);
	ASSERT_DEBUG_STATE(STATE_WARM_RESET_PATH);

	/* Now sleep and move the clock forward to timeout the debug process.
	 * Doing this in two steps verifies that even after the handler executes
	 * "too early" we can still recover via the vup button that's coming
	 * next. This is caused by effectively, sleeping so the scheduler runs,
	 * but not ticking the clock forward yet until the next sleep.
	 */
	k_msleep(11000);
	pass_time(11000);

	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vup 500"));
	pass_time(200);
	ASSERT_DEBUG_STATE(STATE_STAGING);

	pass_time(11000);
	ASSERT_DEBUG_STATE(STATE_DEBUG_NONE);
	zassert_equal(1, chipset_reset_fake.call_count);
	zassert_equal(CHIPSET_RESET_DBG_WARM_REBOOT,
		      chipset_reset_fake.arg0_val);
}

ZTEST(button, test_hc_power_button_press_single)
{
	struct ec_params_power_button_press hc_params = {
		.first_press_delay_ms = 1000,
	};
	struct host_cmd_handler_args hc_press = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_POWER_BUTTON_PRESS, 0, hc_params);

	/* PBTN up on start. */
	zassert_equal(power_button_is_pressed(), 0);

	/* Issue single press. */
	zassert_ok(host_command_process(&hc_press));
	zassert_equal(power_button_is_pressed(), 0);

	/* PBTN up before the 1000ms mark. */
	pass_time(800); /* T + 900ms */
	zassert_equal(power_button_is_pressed(), 0);

	/* PBTN down between 1000-1200ms mark. */
	pass_time(0); /* T + 1000ms */
	zassert_equal(power_button_is_pressed(), 1);
	pass_time(0); /* T + 1100ms */
	zassert_equal(power_button_is_pressed(), 1);

	/* PBTN up afterwards. */
	pass_time(0); /* T + 1200ms */
	zassert_equal(power_button_is_pressed(), 0);
	pass_time(0); /* T + 1300ms */
	zassert_equal(power_button_is_pressed(), 0);
}

ZTEST(button, test_hc_power_button_press_single_custom_duration)
{
	struct ec_params_power_button_press hc_params = {
		.first_press_delay_ms = 1000,
		.first_press_duration_ms = 500,
		.second_press_duration_ms = 300, /* shouldn't be used. */
	};
	struct host_cmd_handler_args hc_press = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_POWER_BUTTON_PRESS, 0, hc_params);

	/* PBTN up on start. */
	zassert_equal(power_button_is_pressed(), 0);

	/* Issue single press. */
	zassert_ok(host_command_process(&hc_press));
	zassert_equal(power_button_is_pressed(), 0);

	/* PBTN up before the 1000ms mark. */
	pass_time(800); /* T + 900ms */
	zassert_equal(power_button_is_pressed(), 0);

	/* PBTN down between 1000-1500ms mark. */
	pass_time(0); /* T + 1000ms */
	zassert_equal(power_button_is_pressed(), 1);
	pass_time(300); /* T + 1400ms */
	zassert_equal(power_button_is_pressed(), 1);

	/* PBTN up afterwards. */
	pass_time(0); /* T + 1500ms */
	zassert_equal(power_button_is_pressed(), 0);
	pass_time(0); /* T + 1600ms */
	zassert_equal(power_button_is_pressed(), 0);
}

ZTEST(button, test_hc_power_button_press_double)
{
	struct ec_params_power_button_press hc_params = {
		.first_press_delay_ms = 1000,
		.second_press_delay_ms = 5000,
	};
	struct host_cmd_handler_args hc_press = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_POWER_BUTTON_PRESS, 0, hc_params);

	/* PBTN up on start. */
	zassert_equal(power_button_is_pressed(), 0);

	/* Issue double press. */
	zassert_ok(host_command_process(&hc_press));
	zassert_equal(power_button_is_pressed(), 0);

	/* PBTN up before the 1000ms mark. */
	pass_time(800); /* T + 900ms */
	zassert_equal(power_button_is_pressed(), 0);

	/* PBTN down between 1000-1200ms mark. */
	pass_time(0); /* T + 1000ms */
	zassert_equal(power_button_is_pressed(), 1);
	pass_time(0); /* T + 1100ms */
	zassert_equal(power_button_is_pressed(), 1);

	/* PBTN up between 1200-5000ms marks. */
	pass_time(0); /* T + 1200ms */
	zassert_equal(power_button_is_pressed(), 0);
	pass_time(3600); /* T + 4900ms */
	zassert_equal(power_button_is_pressed(), 0);

	/* PBTN down between 5000-5200ms marks. */
	pass_time(0); /* T + 5000ms */
	zassert_equal(power_button_is_pressed(), 1);
	pass_time(0); /* T + 5100ms */
	zassert_equal(power_button_is_pressed(), 1);

	/* PBTN up after the 5200ms mark. */
	pass_time(0); /* T + 5200ms */
	zassert_equal(power_button_is_pressed(), 0);
}

ZTEST(button, test_hc_power_button_press_double_custom_duration)
{
	struct ec_params_power_button_press hc_params = {
		.first_press_delay_ms = 1000,
		.second_press_delay_ms = 5000,
		.first_press_duration_ms = 500,
		.second_press_duration_ms = 300,
	};
	struct host_cmd_handler_args hc_press = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_POWER_BUTTON_PRESS, 0, hc_params);

	/* PBTN up on start. */
	zassert_equal(power_button_is_pressed(), 0);

	/* Issue double press. */
	zassert_ok(host_command_process(&hc_press));
	zassert_equal(power_button_is_pressed(), 0);

	/* PBTN up before the 1000ms mark. */
	pass_time(800); /* T + 900ms */
	zassert_equal(power_button_is_pressed(), 0);

	/* PBTN down between 1000-1500ms mark. */
	pass_time(0); /* T + 1000ms */
	zassert_equal(power_button_is_pressed(), 1);
	pass_time(300); /* T + 1400ms */
	zassert_equal(power_button_is_pressed(), 1);

	/* PBTN up between 1500-5000ms marks. */
	pass_time(0); /* T + 1500ms */
	zassert_equal(power_button_is_pressed(), 0);
	pass_time(3300); /* T + 4900ms */
	zassert_equal(power_button_is_pressed(), 0);

	/* PBTN down between 5000-5300ms marks. */
	pass_time(0); /* T + 5000ms */
	zassert_equal(power_button_is_pressed(), 1);
	pass_time(100); /* T + 5200ms */
	zassert_equal(power_button_is_pressed(), 1);

	/* PBTN up after the 5300ms mark. */
	pass_time(0); /* T + 5300ms */
	zassert_equal(power_button_is_pressed(), 0);
}

ZTEST(button, test_hc_power_button_press_invalid_params)
{
	/* First + first duration == second. */
	struct ec_params_power_button_press hc_params = {
		.first_press_delay_ms = 1000,
		.second_press_delay_ms = 2000,
		.first_press_duration_ms = 1000,
	};
	struct host_cmd_handler_args hc_press = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_POWER_BUTTON_PRESS, 0, hc_params);

	zassert_equal(host_command_process(&hc_press), EC_RES_INVALID_PARAM);

	/* First + first duration > second. */
	struct ec_params_power_button_press hc_params_2 = {
		.first_press_delay_ms = 1000,
		.second_press_delay_ms = 1500,
		.first_press_duration_ms = 1000,
	};
	struct host_cmd_handler_args hc_press_2 = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_POWER_BUTTON_PRESS, 0, hc_params_2);

	zassert_equal(host_command_process(&hc_press_2), EC_RES_INVALID_PARAM);

	struct ec_params_power_button_press hc_params_3 = {
		.first_press_delay_ms = 1000,
		.second_press_delay_ms = 1100,
	};
	struct host_cmd_handler_args hc_press_3 = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_POWER_BUTTON_PRESS, 0, hc_params_3);

	zassert_equal(host_command_process(&hc_press_3), EC_RES_INVALID_PARAM);

	/* Second < First. */
	struct ec_params_power_button_press hc_params_4 = {
		.first_press_delay_ms = 2000,
		.second_press_delay_ms = 1000,
	};
	struct host_cmd_handler_args hc_press_4 = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_POWER_BUTTON_PRESS, 0, hc_params_4);

	zassert_equal(host_command_process(&hc_press_4), EC_RES_INVALID_PARAM);
}
