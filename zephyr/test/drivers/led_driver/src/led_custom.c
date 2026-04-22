/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/led.h"
#include "gpio.h"
#include "hooks.h"
#include "led_common.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/ztest.h>

extern uint32_t led_test_apply_count;

void set_board_led_alt_policy(int label);

static void *led_custom_setup(void)
{
	/* Allow init time to settle */
	k_sleep(K_MSEC(1000));

	return NULL;
}

static bool is_blue_on(void)
{
	return gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_chg_led_y_c0));
}

static bool is_white_on(void)
{
	return gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_chg_led_w_c0));
}

static bool is_off(void)
{
	return !is_blue_on() && !is_white_on();
}

static void led_custom_before(void *data)
{
	/* Reset to a known state (White) */
	set_board_led_alt_policy(1);
	led_control(EC_LED_ID_BATTERY_LED, LED_STATE_RESET);
	led_set_custom_patterns(NULL);
	led_test_apply_count = 0;

	/* Run one tick to apply the reset state */
	hook_notify(HOOK_TICK);
	k_sleep(K_MSEC(30));
	zassert_true(is_white_on(), "Setup failed: LED should be White");
}

ZTEST_SUITE(led_driver_custom, drivers_predicate_post_main, led_custom_setup,
	    led_custom_before, NULL, NULL);

/*
 * Test that a custom pattern overrides the standard policy immediately.
 */
ZTEST(led_driver_custom, test_custom_override)
{
	/* Define a custom pattern: Solid Blue, infinite */
	const struct led_pins_node_t *blue_node =
		led_get_node(LED_BLUE, EC_LED_ID_BATTERY_LED);
	zassert_not_null(blue_node, "Could not find Blue node for Battery LED");

	struct pattern_color_node_t color_step = {
		.color_idx = blue_node->color_idx,
		.duration_ms = 1000,
	};

	struct led_pattern_node_t pattern = {
		.led_id = EC_LED_ID_BATTERY_LED,
		.pattern_color = &color_step,
		.pattern_len = 1,
		.cycle_limit = 0, /* Infinite */
		.transition = LED_TRANSITION_STEP,
	};

	struct custom_led_patterns_t custom = {
		.led_patterns = &pattern,
		.num_patterns = 1,
		.led_id = EC_LED_ID_BATTERY_LED,
	};

	/* Apply custom pattern */
	led_set_custom_patterns(&custom);

	/* Allow worker to run */
	k_sleep(K_MSEC(30));

	/* Verify Blue (Custom) overrides White (Policy) */
	zassert_true(is_blue_on(), "Custom pattern did not take effect");
	zassert_false(is_white_on(), "Standard policy should be skipped");

	/* Clear custom pattern */
	led_set_custom_patterns(NULL);

	/* Allow worker to run */
	k_sleep(K_MSEC(30));

	/* Verify return to White */
	zassert_true(is_white_on(), "Should revert to standard policy");
}

/*
 * Test auto reversion after cycle limit.
 */
ZTEST(led_driver_custom, test_custom_auto_revert)
{
	const struct led_pins_node_t *blue_node =
		led_get_node(LED_BLUE, EC_LED_ID_BATTERY_LED);
	const struct led_pins_node_t *off_node =
		led_get_node(LED_OFF, EC_LED_ID_BATTERY_LED);

	struct pattern_color_node_t steps[] = {
		{ .color_idx = blue_node->color_idx, .duration_ms = 100 },
		{ .color_idx = off_node->color_idx, .duration_ms = 100 },
	};

	struct led_pattern_node_t pattern = {
		.led_id = EC_LED_ID_BATTERY_LED,
		.pattern_color = steps,
		.pattern_len = 2,
		.cycle_limit = 1, /* Run once then stop */
		.transition = LED_TRANSITION_STEP,
	};

	struct custom_led_patterns_t custom = {
		.led_patterns = &pattern,
		.num_patterns = 1,
		.led_id = EC_LED_ID_BATTERY_LED,
	};

	/* Apply custom pattern */
	led_set_custom_patterns(&custom);

	/* T=0-100ms: Blue */
	k_sleep(K_MSEC(30));
	zassert_true(is_blue_on(), "Custom pattern step 1 (Blue) failed");

	/* T=100-200ms: Off */
	k_sleep(K_MSEC(100));
	zassert_true(is_off(), "Custom pattern step 2 (Off) failed");

	/* T>200ms: Finished. Should revert to standard policy (White) */
	k_sleep(K_MSEC(130));

	zassert_true(is_white_on(), "Did not revert to standard policy");
}

/*
 * Test that a custom pattern for an LED not managed by the driver is ignored.
 */
ZTEST(led_driver_custom, test_custom_unsupported_led)
{
	const struct led_pins_node_t *blue_node =
		led_get_node(LED_BLUE, EC_LED_ID_BATTERY_LED);

	struct pattern_color_node_t color_step = {
		.color_idx = blue_node->color_idx,
		.duration_ms = 1000,
	};

	struct led_pattern_node_t pattern = {
		.pattern_color = &color_step,
		.pattern_len = 1,
		.cycle_limit = 0,
		.transition = LED_TRANSITION_STEP,
	};

	struct custom_led_patterns_t custom = {
		.led_id = EC_LED_ID_POWER_LED,
		.led_patterns = &pattern,
		.num_patterns = 1,
		.led_id = EC_LED_ID_POWER_LED, /* Unsupported LED */
	};

	/* Apply custom pattern */
	led_set_custom_patterns(&custom);

	/* Trigger Tick */
	hook_notify(HOOK_TICK);
	k_sleep(K_MSEC(30));

	/*
	 * If the mask check failed (bug), the driver would execute the pattern
	 * using blue_node (which points to Battery LED pins) and turn it Blue.
	 * If the mask check works (correct), the pattern is skipped, and
	 * standard policy (White) remains.
	 */
	zassert_true(is_white_on(), "Standard policy should remain active");
}

/*
 * Test that manual control.
 */
ZTEST(led_driver_custom, test_manual_control)
{
	/* Initialize the LED to the OFF state. */
	set_board_led_alt_policy(7);
	hook_notify(HOOK_TICK);
	k_sleep(K_MSEC(30));

	zassert_true(is_off(), "LED intially should be in the off state");

	const struct led_pins_node_t *blue_node =
		led_get_node(LED_BLUE, EC_LED_ID_BATTERY_LED);
	const struct led_pins_node_t *white_node =
		led_get_node(LED_WHITE, EC_LED_ID_BATTERY_LED);

	/* Define custom pattern: Blue (500ms) -> White (500ms) */
	struct pattern_color_node_t steps[] = {
		{ .color_idx = blue_node->color_idx, .duration_ms = 500 },
		{ .color_idx = white_node->color_idx, .duration_ms = 500 },
	};
	struct led_pattern_node_t pattern = {
		.pattern_color = steps,
		.pattern_len = 2,
		.cycle_limit = 0,
		.transition = LED_TRANSITION_STEP,
	};
	struct custom_led_patterns_t custom = {
		.led_id = EC_LED_ID_BATTERY_LED,
		.led_patterns = &pattern,
		.num_patterns = 1,
		.led_id = EC_LED_ID_BATTERY_LED,
	};

	/* Apply custom pattern */
	led_set_custom_patterns(&custom);
	k_sleep(K_MSEC(30));
	zassert_true(is_blue_on(), "Custom pattern step 1 (Blue) failed");

	/* Disable Auto Control */
	led_auto_control(EC_LED_ID_BATTERY_LED, 0);

	/*
	 * Wait long enough that we WOULD have transitioned to White
	 * if auto control were on (>500ms).
	 */
	k_sleep(K_MSEC(600));
	/* Trigger tick to allow driver to (not) run */
	hook_notify(HOOK_TICK);

	/*
	 * Verify LED is still Blue. The driver should not have updated the
	 * hardware to White because auto-control was off.
	 */
	zassert_true(is_blue_on(),
		     "LED should hold Step 1 while auto-control is off");
	zassert_false(is_white_on(),
		      "LED should not have advanced to White (Step 2)");

	/* Re-enable auto control */
	led_auto_control(EC_LED_ID_BATTERY_LED, 1);
	hook_notify(HOOK_TICK);
	k_sleep(K_MSEC(30));

	/*
	 * Verify LED pattern reverts to OFF with auto-control back on.
	 */
	zassert_true(
		is_off(),
		"LED should revert to the off state after auto-control is re-enabled");
}

/*
 * Test system state preemption.
 */
ZTEST(led_driver_custom, test_system_state_preemption)
{
	const struct led_pins_node_t *blue_node =
		led_get_node(LED_BLUE, EC_LED_ID_BATTERY_LED);

	struct pattern_color_node_t color_step = {
		.color_idx = blue_node->color_idx,
		.duration_ms = 5000,
	};

	struct led_pattern_node_t pattern = {
		.led_id = EC_LED_ID_BATTERY_LED,
		.pattern_color = &color_step,
		.pattern_len = 1,
		.cycle_limit = 0, /* Infinite */
		.transition = LED_TRANSITION_STEP,
	};

	struct custom_led_patterns_t custom = {
		.led_patterns = &pattern,
		.num_patterns = 1,
		.led_id = EC_LED_ID_BATTERY_LED,
	};

	/* Apply custom pattern (Blue) */
	led_set_custom_patterns(&custom);
	k_sleep(K_MSEC(30));
	zassert_true(is_blue_on(), "Custom pattern not active");

	/* Change system state to trigger a new policy (Off) */
	set_board_led_alt_policy(7);

	/*
	 * Trigger the tick. match_node should detect the state change,
	 * see the new policy (Off) becoming active, and cancel the custom
	 * pattern.
	 */
	hook_notify(HOOK_TICK);
	k_sleep(K_MSEC(30));

	/* Verify Custom Pattern was cancelled and new policy is active */
	zassert_true(is_off(), "Custom pattern should be preempted");
}

/*
 * Test that standard policy resets cleanly when custom pattern is cleared.
 */
ZTEST(led_driver_custom, test_resume_resets_policy)
{
	/* Set Standard Policy to Infinite Blink (Blue 500ms, White 500ms) */
	set_board_led_alt_policy(2);
	led_control(EC_LED_ID_BATTERY_LED, LED_STATE_RESET);
	led_set_custom_patterns(NULL);

	/* Tick to start */
	hook_notify(HOOK_TICK);
	k_sleep(K_MSEC(30));
	zassert_true(is_blue_on(), "Standard policy should start Blue");

	/* Advance time into the second half of the cycle (White) */
	/* 500ms Blue -> 500ms White. Wait 600ms total. */
	k_sleep(K_MSEC(600));
	zassert_true(is_white_on(), "Standard policy should be White");

	/* Set Custom Pattern (Solid Off) */
	const struct led_pins_node_t *off_node =
		led_get_node(LED_OFF, EC_LED_ID_BATTERY_LED);
	struct pattern_color_node_t color_step = {
		.color_idx = off_node->color_idx,
		.duration_ms = 1000,
	};
	struct led_pattern_node_t pattern = {
		.led_id = EC_LED_ID_BATTERY_LED,
		.pattern_color = &color_step,
		.pattern_len = 1,
		.cycle_limit = 0,
		.transition = LED_TRANSITION_STEP,
	};
	struct custom_led_patterns_t custom = {
		.led_patterns = &pattern,
		.num_patterns = 1,
		.led_id = EC_LED_ID_BATTERY_LED,
	};

	led_set_custom_patterns(&custom);
	k_sleep(K_MSEC(30));
	zassert_true(is_off(), "Custom pattern (Off) active");

	/* Clear Custom Pattern */
	led_set_custom_patterns(NULL);
	k_sleep(K_MSEC(30));

	/* Verify Standard Policy restarted from beginning (Blue) */
	zassert_true(is_blue_on(), "Standard policy should reset to Blue");
}

/*
 * Test that system steady-state does not preempt custom pattern.
 */
ZTEST(led_driver_custom, test_steady_state_persistence)
{
	const struct led_pins_node_t *blue_node =
		led_get_node(LED_BLUE, EC_LED_ID_BATTERY_LED);

	struct pattern_color_node_t color_step = {
		.color_idx = blue_node->color_idx,
		.duration_ms = 5000,
	};

	struct led_pattern_node_t pattern = {
		.led_id = EC_LED_ID_BATTERY_LED,
		.pattern_color = &color_step,
		.pattern_len = 1,
		.cycle_limit = 0,
		.transition = LED_TRANSITION_STEP,
	};

	struct custom_led_patterns_t custom = {
		.led_patterns = &pattern,
		.num_patterns = 1,
		.led_id = EC_LED_ID_BATTERY_LED,
	};

	/* Trigger a policy and settle its state */
	set_board_led_alt_policy(7);
	hook_notify(HOOK_TICK);
	k_sleep(K_MSEC(30));

	/* Apply custom pattern (Blue) */
	led_set_custom_patterns(&custom);
	k_sleep(K_MSEC(30));
	zassert_true(is_blue_on());

	/* Trigger tick without changing system state */
	hook_notify(HOOK_TICK);
	k_sleep(K_MSEC(30));

	/* Verify Custom Pattern is still active */
	zassert_true(is_blue_on(), "Custom pattern should persist");
	zassert_false(is_white_on(), "Standard policy should not override");
}

/*
 * Test replacing one active custom pattern with another.
 */
ZTEST(led_driver_custom, test_custom_replace_custom)
{
	set_board_led_alt_policy(7);
	hook_notify(HOOK_TICK);
	k_sleep(K_MSEC(30));
	zassert_true(is_off(), "Base policy should be Off");

	const struct led_pins_node_t *blue_node =
		led_get_node(LED_BLUE, EC_LED_ID_BATTERY_LED);
	const struct led_pins_node_t *white_node =
		led_get_node(LED_WHITE, EC_LED_ID_BATTERY_LED);

	/* Define Pattern 1: Solid Blue */
	struct pattern_color_node_t color_step_1 = {
		.color_idx = blue_node->color_idx,
		.duration_ms = 1000,
	};
	struct led_pattern_node_t pattern_1 = {
		.led_id = EC_LED_ID_BATTERY_LED,
		.pattern_color = &color_step_1,
		.pattern_len = 1,
		.cycle_limit = 0,
		.transition = LED_TRANSITION_STEP,
	};
	struct custom_led_patterns_t custom_1 = {
		.led_patterns = &pattern_1,
		.num_patterns = 1,
		.led_id = EC_LED_ID_BATTERY_LED,
	};

	/* Define Pattern 2: Solid White */
	struct pattern_color_node_t color_step_2 = {
		.color_idx = white_node->color_idx,
		.duration_ms = 1000,
	};
	struct led_pattern_node_t pattern_2 = {
		.led_id = EC_LED_ID_BATTERY_LED,
		.pattern_color = &color_step_2,
		.pattern_len = 1,
		.cycle_limit = 0,
		.transition = LED_TRANSITION_STEP,
	};
	struct custom_led_patterns_t custom_2 = {
		.led_patterns = &pattern_2,
		.num_patterns = 1,
		.led_id = EC_LED_ID_BATTERY_LED,
	};

	/* Apply Custom 1 (Blue) */
	led_set_custom_patterns(&custom_1);
	k_sleep(K_MSEC(30));
	zassert_true(is_blue_on(), "Custom 1 failed");

	/* Apply Custom 2 (White) immediately */
	led_set_custom_patterns(&custom_2);
	k_sleep(K_MSEC(30));
	zassert_true(is_white_on(), "Custom 2 failed to replace Custom 1");

	/* Clear */
	led_set_custom_patterns(NULL);
	k_sleep(K_MSEC(30));
	zassert_true(is_off(), "Failed to clear custom pattern");
}
