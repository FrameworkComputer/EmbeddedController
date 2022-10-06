/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#include "console.h"
#include "ec_commands.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

ZTEST_USER(console_cmd_tcpci_dump, test_no_params)
{
	int rv = shell_execute_cmd(get_ec_shell(), "tcpci_dump");

	zassert_equal(rv, EC_ERROR_PARAM_COUNT, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);
}

ZTEST_USER(console_cmd_tcpci_dump, test_good_index)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "tcpci_dump 0"),
		   "Failed index 0 print");
}

ZTEST_USER(console_cmd_tcpci_dump, test_bad_index)
{
	int rv = shell_execute_cmd(get_ec_shell(), "tcpci_dump 84");

	zassert_equal(rv, EC_ERROR_INVAL, "Expected %d, but got %d",
		      EC_ERROR_INVAL, rv);
}

static void console_cmd_tcpci_dump_begin(void *data)
{
	ARG_UNUSED(data);

	/* Assume we have at least one TCPC */
	zassume_true(board_get_charger_chip_count() > 0,
		     "Insufficient TCPCs found");
}

ZTEST_SUITE(console_cmd_tcpci_dump, drivers_predicate_post_main, NULL,
	    console_cmd_tcpci_dump_begin, NULL, NULL);
