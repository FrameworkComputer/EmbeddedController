/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#include "console.h"
#include "ec_commands.h"
#include "include/lpc.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#ifdef CONFIG_HOST_EVENT64
#define HOSTEVENT_PRINT_FORMAT "016" PRIx64
#else
#define HOSTEVENT_PRINT_FORMAT "08" PRIx32
#endif

struct console_cmd_hostevent_fixture {
	struct host_events_ctx ctx;
};

static void *console_cmd_hostevent_setup(void)
{
	static struct console_cmd_hostevent_fixture fixture = { 0 };

	return &fixture;
}

static void console_cmd_hostevent_before(void *fixture)
{
	struct console_cmd_hostevent_fixture *f = fixture;

	host_events_save(&f->ctx);
}

static void console_cmd_hostevent_after(void *fixture)
{
	struct console_cmd_hostevent_fixture *f = fixture;

	host_events_restore(&f->ctx);
}

static int console_cmd_hostevent(const char *subcommand, host_event_t mask)
{
	int rv;
	char cmd_buf[CONFIG_SHELL_CMD_BUFF_SIZE];

	rv = snprintf(cmd_buf, CONFIG_SHELL_CMD_BUFF_SIZE,
		      "hostevent %s 0x%" HOSTEVENT_PRINT_FORMAT, subcommand,
		      mask);

	zassume_between_inclusive(rv, 0, CONFIG_SHELL_CMD_BUFF_SIZE,
				  "hostevent console command too long");

	return shell_execute_cmd(get_ec_shell(), cmd_buf);
}

/* hostevent with no arguments */
ZTEST_USER(console_cmd_hostevent, test_hostevent)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "hostevent"),
		   "Failed default print");
}

/* hostevent with invalid arguments */
ZTEST_USER(console_cmd_hostevent, test_hostevent_invalid)
{
	int rv;
	host_event_t mask = 0;

	/* Test invalid sub-command */
	rv = console_cmd_hostevent("invalid", mask);
	zassert_equal(rv, EC_ERROR_PARAM1, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);

	/* Test invalid mask */
	rv = shell_execute_cmd(get_ec_shell(), "hostevent set invalid-mask");
	zassert_equal(rv, EC_ERROR_PARAM2, "Expected %d, but got %d",
		      EC_ERROR_PARAM2, rv);
}

/* hostevent with sub-commands and verification */
ZTEST_USER(console_cmd_hostevent, test_hostevent_sub_commands)
{
	int rv;
	enum ec_status ret_val;
	host_event_t event_mask;
	host_event_t all_events = 0;
	host_event_t set_events;
	struct ec_response_host_event result = { 0 };
	struct {
		enum lpc_host_event_type type;
		const char *name;
		host_event_t mask;
	} subcommand[] = {
		{
			.type = LPC_HOST_EVENT_SMI,
			.name = "SMI",
			.mask = EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_CLOSED),
		},
		{
			.type = LPC_HOST_EVENT_SCI,
			.name = "SCI",
			.mask = EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN),
		},
		{
			.type = LPC_HOST_EVENT_WAKE,
			.name = "WAKE",
			.mask = EC_HOST_EVENT_MASK(EC_HOST_EVENT_POWER_BUTTON),
		},
		{
			.type = LPC_HOST_EVENT_ALWAYS_REPORT,
			.name = "ALWAYS_REPORT",
			.mask = EC_HOST_EVENT_MASK(
				EC_HOST_EVENT_AC_DISCONNECTED),
		},
	};

	for (int i = 0; i < ARRAY_SIZE(subcommand); i++) {
		event_mask = lpc_get_host_event_mask(subcommand[i].type);
		zassert_false(event_mask & subcommand[i].mask,
			      "%s mask is set before test started",
			      subcommand[i].name);
		/*
		 * Setting mask value overwrites existing setting, so OR in
		 * the test bit.
		 */
		event_mask |= subcommand[i].mask;
		rv = console_cmd_hostevent(subcommand[i].name, event_mask);
		zassert_ok(rv, "Subcommand %s failed", subcommand[i].name);
		zassert_true(lpc_get_host_event_mask(subcommand[i].type) &
				     subcommand[i].mask,
			     "Failed to set %s event mask", subcommand[i].name);

		/*
		 * It is only valid to set host events, once at least one mask
		 * value includes the event.  Setting host events preserves
		 * existing events.
		 */
		zassert_false(host_get_events() & subcommand[i].mask,
			      "Host event is set before test started");
		rv = console_cmd_hostevent("set", subcommand[i].mask);
		zassert_ok(rv, "Subcommand SET failed");

		all_events |= subcommand[i].mask;
	}

	/* Verify all host events were set, and none were lost */
	zassert_true((host_get_events() & all_events) == all_events,
		     "Failed to set host events");

	/* Test clearing of host events */
	set_events = all_events;
	for (int i = 0; i < ARRAY_SIZE(subcommand); i++) {
		set_events &= ~subcommand[i].mask;
		rv = console_cmd_hostevent("clear", subcommand[i].mask);
		zassert_ok(rv, "Subcommand CLEAR failed");

		zassert_true((host_get_events() & set_events) == set_events,
			     "Failed to clear host event");
	}

	/* Verify the backup host events were set, and none were cleared */
	ret_val = host_cmd_host_event(EC_HOST_EVENT_GET, EC_HOST_EVENT_B,
				      &result);
	zassert_equal(ret_val, EC_RES_SUCCESS, "Expected=%d, returned=%d",
		      EC_RES_SUCCESS, ret_val);
	zassert_true((result.value & all_events) == all_events,
		     "Failed to set host events backup");

	/* Test clearing of backup host events */
	set_events = all_events;
	for (int i = 0; i < ARRAY_SIZE(subcommand); i++) {
		set_events &= ~subcommand[i].mask;
		rv = console_cmd_hostevent("clearb", subcommand[i].mask);
		zassert_ok(rv, "Subcommand CLEAR failed");

		ret_val = host_cmd_host_event(EC_HOST_EVENT_GET,
					      EC_HOST_EVENT_B, &result);
		zassert_equal(ret_val, EC_RES_SUCCESS,
			      "Expected=%d, returned=%d", EC_RES_SUCCESS,
			      ret_val);
		zassert_true((result.value & set_events) == set_events,
			     "Failed to clear host events backup");
	}
}

ZTEST_SUITE(console_cmd_hostevent, drivers_predicate_post_main,
	    console_cmd_hostevent_setup, console_cmd_hostevent_before,
	    console_cmd_hostevent_after, NULL);
