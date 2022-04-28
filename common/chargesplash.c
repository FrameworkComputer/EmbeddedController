/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdbool.h>
#include <string.h>

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "extpower.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"
#include "power_button.h"
#include "timer.h"
#include "util.h"

#define CPRINTS(format, args...) \
	cprints(CC_USBCHARGE, "chargesplash: " format, ##args)

/*
 * Was this power on initiated to show a charge splash?
 *
 * - Set when powering on for an AC connect.
 * - Unset when power button is pushed, or the chargesplash request is
 *   cancelled due to AC disconnection.
 */
static bool power_on_for_chargesplash;

/* True once the display has come up */
static bool display_initialized;

/*
 * True if the chargesplash is locked out, and we must wait until no
 * requests happen during the chargesplash period until the lockout can
 * be cleared.
 */
static bool locked_out;

/*
 * A circular buffer of the most recent chargesplash request
 * timestamps (stored as an integer value of seconds).
 */
static int request_log[CONFIG_CHARGESPLASH_MAX_REQUESTS_PER_PERIOD];
BUILD_ASSERT(CONFIG_CHARGESPLASH_MAX_REQUESTS_PER_PERIOD >= 1,
	     "There must be at least one request allowed per period");

/*
 * Return true if the timestamp is outside of the tracking period,
 * false otherwise.
 */
static bool timestamp_is_expired(int timestamp, int now)
{
	if (!timestamp) {
		/* The log entry hasn't been filled yet */
		return true;
	}

	return (now - timestamp) >= CONFIG_CHARGESPLASH_PERIOD;
}

/*
 * Returns true only if all timestamps have been expired, or we aren't
 * locked out anyway.
 */
static bool lockout_can_be_cleared(int now)
{
	if (!locked_out)
		return true;

	for (int i = 0; i < ARRAY_SIZE(request_log); i++) {
		if (!timestamp_is_expired(request_log[i], now)) {
			return false;
		}
	}

	return true;
}

/*
 * Write the current time into the request log.  If the request should
 * be permitted to cause a boot, return true.  Otherwise, if the
 * chargesplash should be inhibited, return false.
 */
static bool log_request(void)
{
	static int log_ptr;
	int now = get_time().val / SECOND;
	bool inhibit_boot = false;

	if (lockout_can_be_cleared(now)) {
		locked_out = false;
	} else {
		inhibit_boot = true;
	}

	if (!timestamp_is_expired(request_log[log_ptr], now)) {
		locked_out = true;
		inhibit_boot = true;
	}

	request_log[log_ptr] = now;
	log_ptr = (log_ptr + 1) % ARRAY_SIZE(request_log);
	return !inhibit_boot;
}

/* Manually reset state (via host or UART cmd) */
static void reset_state(void)
{
	power_on_for_chargesplash = false;
	display_initialized = false;
	locked_out = false;
	memset(request_log, 0, sizeof(request_log));
}

static void request_chargesplash(void)
{
	if (!log_request()) {
		CPRINTS("Locked out, request inhibited");
		return;
	}

	CPRINTS("Power on for charge display");
	power_on_for_chargesplash = true;
	display_initialized = false;
	chipset_power_on();
}

static void display_ready(void)
{
	/*
	 * TODO(b/228370390): Consider asserting PROCHOT (on
	 * some platforms) to slow down background boot.
	 */

	CPRINTS("Display initialized");
	display_initialized = true;
}

static void handle_ac_change(void)
{
	if (extpower_is_present() && !power_on_for_chargesplash) {
		if (!lid_is_open()) {
			CPRINTS("Ignore AC connect as lid is closed");
			return;
		}

		if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
			request_chargesplash();
		}
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, handle_ac_change, HOOK_PRIO_LAST - 1);

static void handle_power_button_change(void)
{
	if (power_button_is_pressed()) {
		reset_state();
	}
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, handle_power_button_change,
	     HOOK_PRIO_FIRST);

static void handle_chipset_shutdown(void)
{
	power_on_for_chargesplash = false;
	display_initialized = false;
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, handle_chipset_shutdown, HOOK_PRIO_DEFAULT);

static int command_chargesplash(int argc, char **argv)
{
	if (argc != 2) {
		return EC_ERROR_PARAM_COUNT;
	}

	if (!strcasecmp(argv[1], "state")) {
		ccprintf("requested = %d\n", power_on_for_chargesplash);
		ccprintf("display_initialized = %d\n", display_initialized);
		ccprintf("locked_out = %d\n", locked_out);

		ccprintf("\nRequest log (raw data):\n");
		for (int i = 0; i < ARRAY_SIZE(request_log); i++) {
			ccprintf("  %d\n", request_log[i]);
		}
		return EC_SUCCESS;
	}

	if (!strcasecmp(argv[1], "request")) {
		request_chargesplash();
		return EC_SUCCESS;
	}

	if (!strcasecmp(argv[1], "reset")) {
		reset_state();
		return EC_SUCCESS;
	}

	if (!strcasecmp(argv[1], "lockout")) {
		locked_out = true;
		return EC_SUCCESS;
	}

	return EC_ERROR_PARAM1;
}
DECLARE_CONSOLE_COMMAND(chargesplash, command_chargesplash,
			"[state|request|reset|lockout]",
			"Charge splash controls");

static enum ec_status chargesplash_host_cmd(struct host_cmd_handler_args *args)
{
	const struct ec_params_chargesplash *params = args->params;
	struct ec_response_chargesplash *response = args->response;

	if (args->params_size < sizeof(*params)) {
		return EC_RES_INVALID_PARAM;
	}

	if (args->response_max < sizeof(*response)) {
		return EC_RES_INVALID_RESPONSE;
	}

	switch (params->cmd) {
	case EC_CHARGESPLASH_GET_STATE:
		/* No action to do */
		break;
	case EC_CHARGESPLASH_DISPLAY_READY:
		if (power_on_for_chargesplash) {
			display_ready();
		}
		break;
	case EC_CHARGESPLASH_REQUEST:
		request_chargesplash();
		break;
	case EC_CHARGESPLASH_RESET:
		reset_state();
		break;
	case EC_CHARGESPLASH_LOCKOUT:
		locked_out = true;
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}

	/* All commands return the (possibly updated) state */
	response->requested = power_on_for_chargesplash;
	response->display_initialized = display_initialized;
	response->locked_out = locked_out;
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CHARGESPLASH, chargesplash_host_cmd,
		     EC_VER_MASK(0));
