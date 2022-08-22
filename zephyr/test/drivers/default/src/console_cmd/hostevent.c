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

/* hostevent with no arguments */
ZTEST_USER(console_cmd_hostevent, test_hostevent)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "hostevent"),
		   "Failed default print");
}

ZTEST_SUITE(console_cmd_hostevent, drivers_predicate_post_main,
	    console_cmd_hostevent_setup, console_cmd_hostevent_before,
	    console_cmd_hostevent_after, NULL);
