/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "config.h"
#include "ec_commands.h"
#include "extpower.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "timer.h"

#include <stdbool.h>

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/ztest.h>

/* Do a chargesplash host cmd */
static enum ec_status
chargesplash_hostcmd(enum ec_chargesplash_cmd cmd,
		     struct ec_response_chargesplash *response)
{
	struct ec_params_chargesplash params = { .cmd = cmd };

	return ec_cmd_chargesplash(NULL, &params, response);
}

static bool is_chargesplash_requested(void)
{
	struct ec_response_chargesplash response;

	zassert_ok(chargesplash_hostcmd(EC_CHARGESPLASH_GET_STATE, &response),
		   NULL);

	return response.requested;
}

static struct k_poll_signal s0_signal = K_POLL_SIGNAL_INITIALIZER(s0_signal);
static struct k_poll_event s0_event = K_POLL_EVENT_INITIALIZER(
	K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &s0_signal);

static void handle_chipset_s0_event(void)
{
	k_poll_signal_raise(&s0_signal, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, handle_chipset_s0_event, HOOK_PRIO_LAST);

static void wait_for_chipset_startup(void)
{
	if (!chipset_in_state(CHIPSET_STATE_ON)) {
		k_poll_signal_reset(&s0_signal);
		k_poll(&s0_event, 1, K_FOREVER);
	}

	/*
	 * TODO(b/230362548): We need to give the EC a bit to "calm down"
	 * after reaching S0.
	 */
	crec_msleep(2000);
}

#define GPIO_LID_OPEN_EC_NODE DT_NODELABEL(gpio_lid_open_ec)
#define GPIO_LID_OPEN_EC_CTLR DT_GPIO_CTLR(GPIO_LID_OPEN_EC_NODE, gpios)
#define GPIO_LID_OPEN_EC_PORT DT_GPIO_PIN(GPIO_LID_OPEN_EC_NODE, gpios)

static void set_lid(bool open, bool inhibit_boot)
{
	const struct device *lid_switch_dev =
		DEVICE_DT_GET(GPIO_LID_OPEN_EC_CTLR);

	__ASSERT(lid_is_open() != open,
		 "Lid change was requested, but it's already in that state");

	if (!open) {
		__ASSERT(!inhibit_boot,
			 "inhibit_boot should not be used with a lid close");
	}

	zassert_ok(gpio_emul_input_set(lid_switch_dev, GPIO_LID_OPEN_EC_PORT,
				       open),
		   "Failed to set lid switch GPIO");

	while (lid_is_open() != open) {
		crec_usleep(LID_DEBOUNCE_US + 1);
	}

	if (inhibit_boot) {
		wait_for_chipset_startup();
		test_set_chipset_to_g3();
	}
}

/* Simulate a regular power button press */
static void pulse_power_button(void)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "powerbtn"));
}

static void reset_state(void *unused)
{
	test_set_chipset_to_g3();

	/*
	 * Prevent critical low battery from moving us back to G3 when
	 * lid is opened.
	 */
	test_set_battery_level(75);

	if (lid_is_open()) {
		set_lid(false, false);
	}

	if (extpower_is_present()) {
		set_ac_enabled(false);
	}

	zassert_ok(shell_execute_cmd(get_ec_shell(), "chargesplash reset"),
		   "'chargesplash reset' shell command failed");
}

ZTEST_SUITE(chargesplash, drivers_predicate_post_main, NULL, reset_state, NULL,
	    reset_state);

/*
 * When the lid is open and AC is connected, the chargesplash should
 * be requested.
 */
ZTEST_USER(chargesplash, test_connect_ac)
{
	set_lid(true, true);

	set_ac_enabled(true);
	zassert_true(is_chargesplash_requested(),
		     "chargesplash should be requested");
	wait_for_chipset_startup();
}

/*
 * When AC is not connected and we open the lid, the chargesplash
 * should not be requested.
 */
ZTEST_USER(chargesplash, test_no_connect_ac)
{
	set_lid(true, false);
	zassert_false(is_chargesplash_requested(),
		      "chargesplash should not be requested");
	wait_for_chipset_startup();
}

/*
 * When we connect AC with the lid closed, the chargesplash should not
 * be requested.
 */
ZTEST_USER(chargesplash, test_ac_connect_when_lid_closed)
{
	set_ac_enabled(true);
	zassert_false(is_chargesplash_requested(),
		      "chargesplash should not be requested");
}

/*
 * Test that, after many repeated requests, the chargesplash
 * feature becomes locked and non-functional.  This condition
 * replicates a damaged charger or port which cannot maintain a
 * reliable connection.
 *
 * Then, ensure the lockout clears after the chargesplash period
 * passes.
 */
ZTEST_USER(chargesplash, test_lockout)
{
	int i;

	set_lid(true, true);

	for (i = 0; i < CONFIG_CHARGESPLASH_MAX_REQUESTS_PER_PERIOD; i++) {
		set_ac_enabled(true);

		zassert_true(is_chargesplash_requested(),
			     "chargesplash should be requested");
		wait_for_chipset_startup();

		set_ac_enabled(false);
		test_set_chipset_to_g3();
	}

	set_ac_enabled(true);
	zassert_false(is_chargesplash_requested(),
		      "chargesplash should be locked out");
	set_ac_enabled(false);

	crec_sleep(CONFIG_CHARGESPLASH_PERIOD);

	set_ac_enabled(true);
	zassert_true(is_chargesplash_requested(),
		     "lockout should have cleared");
	wait_for_chipset_startup();
}

/* Test cancel chargesplash request by power button push */
ZTEST_USER(chargesplash, test_power_button)
{
	set_lid(true, true);

	set_ac_enabled(true);
	zassert_true(is_chargesplash_requested(),
		     "chargesplash should be requested");
	wait_for_chipset_startup();
	zassert_true(is_chargesplash_requested(),
		     "chargesplash should still be requested");

	pulse_power_button();
	zassert_false(is_chargesplash_requested(),
		      "chargesplash should be canceled by power button push");
	zassert_true(chipset_in_state(CHIPSET_STATE_ON),
		     "chipset should be on");
}

/* Manually lockout the feature via the shell */
ZTEST_USER(chargesplash, test_manual_lockout_via_console)
{
	/*
	 * Put an entry in the request log so the lockout has
	 * something to wait on to clear.
	 */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "chargesplash request"),
		   NULL);
	zassert_true(is_chargesplash_requested(),
		     "chargesplash should be requested");
	wait_for_chipset_startup();
	test_set_chipset_to_g3();

	zassert_ok(shell_execute_cmd(get_ec_shell(), "chargesplash lockout"),
		   NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "chargesplash request"),
		   NULL);
	zassert_false(is_chargesplash_requested(),
		      "chargesplash should be not requested due to lockout");

	crec_sleep(CONFIG_CHARGESPLASH_PERIOD);

	zassert_ok(shell_execute_cmd(get_ec_shell(), "chargesplash request"),
		   NULL);
	zassert_true(is_chargesplash_requested(),
		     "lockout should have cleared");
	wait_for_chipset_startup();
}

/* Manually lockout the feature via host command */
ZTEST_USER(chargesplash, test_manual_lockout_via_hostcmd)
{
	struct ec_response_chargesplash response;

	zassert_ok(chargesplash_hostcmd(EC_CHARGESPLASH_REQUEST, &response),
		   NULL);
	zassert_true(is_chargesplash_requested(),
		     "chargesplash should be requested");
	wait_for_chipset_startup();
	test_set_chipset_to_g3();

	zassert_ok(chargesplash_hostcmd(EC_CHARGESPLASH_LOCKOUT, &response),
		   NULL);
	zassert_ok(chargesplash_hostcmd(EC_CHARGESPLASH_REQUEST, &response),
		   NULL);
	zassert_false(is_chargesplash_requested(),
		      "chargesplash should be not requested due to lockout");

	crec_sleep(CONFIG_CHARGESPLASH_PERIOD);

	zassert_ok(chargesplash_hostcmd(EC_CHARGESPLASH_REQUEST, &response),
		   NULL);
	zassert_true(is_chargesplash_requested(),
		     "lockout should have cleared");
	wait_for_chipset_startup();
}

/* Simulate an actual run of the display loop */
ZTEST_USER(chargesplash, test_display_loop)
{
	struct ec_response_chargesplash response;

	set_lid(true, true);
	set_ac_enabled(true);
	zassert_true(is_chargesplash_requested());
	wait_for_chipset_startup();

	zassert_ok(chargesplash_hostcmd(EC_CHARGESPLASH_DISPLAY_READY,
					&response),
		   NULL);

	zassert_true(is_chargesplash_requested());
	pulse_power_button();
	zassert_false(is_chargesplash_requested());
}
