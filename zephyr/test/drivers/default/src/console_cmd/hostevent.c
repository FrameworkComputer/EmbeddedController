/* Copyright 2022 The ChromiumOS Authors.
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

ZTEST_SUITE(console_cmd_hostevent, drivers_predicate_post_main,
	    console_cmd_hostevent_setup, console_cmd_hostevent_before,
	    console_cmd_hostevent_after, NULL);
