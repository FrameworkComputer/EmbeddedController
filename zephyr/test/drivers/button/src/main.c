/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#include "button.h"
#include "console.h"
#include "hooks.h"
#include "test/drivers/test_state.h"
#include "timer.h"

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
}

ZTEST_SUITE(button, drivers_predicate_post_main, button_setup, button_before,
	    NULL, NULL);

static inline void pass_time(uint64_t duration_ms)
{
	get_time_mock->val += duration_ms * 1000;
	k_msleep(duration_ms);
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

	/* Let the deferred calls get run (800ms) */
	pass_time(800);
	ASSERT_DEBUG_STATE(STATE_DEBUG_CHECK);

	/* Wait for the timeout */
	pass_time(11000);
	ASSERT_DEBUG_STATE(STATE_DEBUG_NONE);
}

ZTEST(button, test_fail_check_button_released_too_soon)
{
	/* Press both volume-up and volume-down for 0.9 seconds */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vup 9000"));
	zassert_ok(shell_execute_cmd(get_ec_shell(), "button vdown 9000"));

	/* Let the deferred calls get run (800ms) */
	pass_time(800);
	ASSERT_DEBUG_STATE(STATE_DEBUG_CHECK);

	/* Wait for the timeout, should put us in staging */
	pass_time(11000);
	ASSERT_DEBUG_STATE(STATE_STAGING);

	/* Wait for the handler to be called and set us to ACTIVE mode */
	pass_time(7000);
	ASSERT_DEBUG_STATE(STATE_DEBUG_MODE_ACTIVE);

	/* Wait for the deadline to pass, putting us back in NONE */
	pass_time(11000);
	ASSERT_DEBUG_STATE(STATE_DEBUG_NONE);
}
