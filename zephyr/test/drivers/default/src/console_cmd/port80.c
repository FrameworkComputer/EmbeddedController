/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Unit Tests for ESPI port 80 console command
 */

#include "console.h"
#include "ec_commands.h"
#include "hooks.h"
#include "port80.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "zephyr/sys/util.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(port80_console);

/**
 * @brief TestPurpose: Verify port 80 console commands
 *
 * @details
 * Validate that the port 80 console commands work.
 *
 * Expected Results
 *  - The port 80 console commands return the appropriate result
 */
ZTEST(console_cmd_port80, test_port80_console_subcmd)
{
	CHECK_CONSOLE_CMD("port80 scroll", "scroll enabled", EC_SUCCESS);
	CHECK_CONSOLE_CMD("port80 scroll", "scroll disabled", EC_SUCCESS);

	zassert_ok(!shell_execute_cmd(get_ec_shell(), "port80 unknown_param"),
		   NULL);
}

static const char port80_header[] = "Port 80 writes:\r\n";
static const char port80_footer[] = "<--new\r\n";
static const char port80_empty_result[] = "Port 80 writes:\r\n <--new";

/**
 * @brief Helper routine for checking for a complete Port 80 message output
 * which consists of a header, 0 or more port 80 codes, and a footer.
 *
 * @param head Start of the port 80 codes to verify.
 * @param tail end of the port 80 codes to verify.
 * @param port80_buffer Set to the shell output buffer pointing to the start
 * of the port 80 output so caller can perform additional checks.
 * @return 0 on success
 */
int check_port80_output(int head, int tail, const char **port80_buffer)
{
	const char *buffer;
	size_t buffer_size;
	char port_80_str[3];

	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	/* Search for the Port 80 output header.  If found, advance
	 * the search location.
	 */
	buffer = strstr(buffer, port80_header);
	if (buffer == NULL) {
		LOG_ERR("DUT failed to display port 80 codes header");
		return -ENODATA;
	}

	/* Set the caller's buffer to the start of the port80 output. */
	*port80_buffer = buffer;

	for (int i = head; i < tail; i++) {
		snprintf(port_80_str, sizeof(port_80_str), "%02x", i);
		/* Advance the search location to ensure port 80 codes
		 * displayed in order.
		 */
		buffer = strstr(buffer, port_80_str);
		if (buffer == NULL) {
			LOG_ERR("DUT failed to display port 80 code %02x", i);
			return -ENODATA;
		}
	}

	if (strstr(buffer, port80_footer) == NULL) {
		LOG_ERR("DUT failed to display port 80 footer");
		return -ENODATA;
	}

	return 0;
}

ZTEST(console_cmd_port80, test_port80_console)
{
	int head = 0;
	int tail = 0;
	int code_from_int;
	char port_80_str[3];
	const char *buffer;
	size_t buffer_size;

	/* Start with an empty history. */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "port80 flush"), NULL);
	CHECK_CONSOLE_CMD("port80", port80_empty_result, EC_SUCCESS);

	/* Write a few codes, the shell should automatically display them
	 * after a delay.
	 */
	shell_backend_dummy_clear_output(get_ec_shell());

	for (int i = tail; i < 10; i++) {
		port_80_write(tail++);
	}

	k_sleep(K_MSEC(10));
	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_is_null(strstr(buffer, port80_header),
			"DUT failed to delay port 80");

	k_sleep(K_SECONDS(5));
	zassert_ok(check_port80_output(head, tail, &buffer),
		   "DUT failed to display port 80 codes automatically");

	/* Turn on printing from an interrupt. Port 80 codes should display
	 * immediately.
	 */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "port80 intprint"), NULL);
	code_from_int = tail;
	port_80_write(tail++);

	k_sleep(K_MSEC(10));
	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	snprintf(port_80_str, sizeof(port_80_str), "%02x", code_from_int);
	zassert_not_null(strstr(buffer, "Port 80:"),
			 "DUT failed to display port 80 from interrupt");
	zassert_not_null(
		strstr(buffer, port_80_str),
		"DUT failed to display port 80 code %02x from interrupt",
		code_from_int);

	/* Turn off printing from an interrupt. */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "port80 intprint"), NULL);

	shell_backend_dummy_clear_output(get_ec_shell());

	/* Print enough codes to fill one line of output. */
	for (int i = tail; i < 20; i++) {
		port_80_write(tail++);
	}

	/* We've already tested the automatic printing.  Call port80 directly
	 * so we don't have to wait.
	 */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "port80"), NULL);
	zassert_ok(check_port80_output(head, tail, &buffer),
		   "DUT failed to display port 80 codes");

	/* Special codes generate different output. */
	shell_backend_dummy_clear_output(get_ec_shell());
	port_80_write(tail++);
	/* Chipset resume should generate a port80 code. */
	hook_notify(HOOK_CHIPSET_RESUME);
	port_80_write(tail++);
	port_80_write(PORT_80_EVENT_RESET);

	zassert_ok(shell_execute_cmd(get_ec_shell(), "port80"), NULL);
	zassert_ok(check_port80_output(head, tail, &buffer),
		   "DUT failed to display port 80 codes");
	/* Check for PORT_80_EVENT_RESUME. */
	zassert_not_null(strstr(buffer, "(S3->S0)"),
			 "DUT failed to display port 80 resume event");
	/* Check for PORT_80_EVENT_RESET. */
	zassert_not_null(strstr(buffer, "(RESET)"),
			 "DUT failed to display port 80 reset event");

	/* Overflow the history. Oldest entries are discarded. */
	head = tail;
	for (int i = tail; i < CONFIG_PORT80_HISTORY_LEN + 2; i++) {
		port_80_write(tail++);
	}

	zassert_ok(shell_execute_cmd(get_ec_shell(), "port80"), NULL);
	zassert_ok(check_port80_output(head, tail, &buffer),
		   "DUT failed to display port 80 codes after overflow");

	/* Verify we can clear the history. */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "port80 flush"), NULL);
	CHECK_CONSOLE_CMD("port80", port80_empty_result, EC_SUCCESS);
}

/**
 * @brief Test Suite: Verifies port 80 console commands.
 */
ZTEST_SUITE(console_cmd_port80, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
