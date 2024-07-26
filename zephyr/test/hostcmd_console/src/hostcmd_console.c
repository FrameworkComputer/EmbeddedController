/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "host_command.h"
#include "test_state.h"
#include "uart.h"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

/* Note - the logging subsystem adds a \r after \n characters for all
 * log modes, except LOG_MODE_MINIMAL. The test purposely skips including
 * a newline test messages.
 */
#define LOG_TEST_MSG "EC output via logging"

/** Messages used in test */
static const char msg1[] = "test";
static const char msg2[] = "hostcmd console";
static const char msg3[] = LOG_TEST_MSG;

/** Length of message excluding NULL char at the end */
#define MSG_LEN(msg) (sizeof(msg) - 1)

/**
 * Write message 1 before first snapshot. Read everything from buffer. Create
 * second snapshot. Write message 2 after it.
 */
static void setup_snapshots_and_messages(void *unused)
{
	char response[1024];
	struct host_cmd_handler_args read_args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_CONSOLE_READ, 0, response);

	/* Set first snapshot before first message */
	zassert_equal(EC_RES_SUCCESS, ec_cmd_console_snapshot(NULL));
	cputs(CC_SYSTEM, msg1);

	/* Read everything from buffer */
	do {
		/* Clear response size before executing command */
		read_args.response_size = 0;
		zassert_equal(EC_RES_SUCCESS, host_command_process(&read_args),
			      NULL);
	} while (read_args.response_size != 0);

	/* Set second snapshot after first message */
	zassert_equal(EC_RES_SUCCESS, ec_cmd_console_snapshot(NULL));
	cputs(CC_SYSTEM, msg2);
}

/**
 * Test if read next variant of console read host command is working. ver
 * argument allows to change version of command. In case of version 1, parameter
 * with sub command is provided.
 */
static void test_uart_hc_read_next(int ver)
{
	/* Should be able to read whole buffer in one command */
	char response[CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_BUF_SIZE + 1];
	struct ec_params_console_read_v1 params;
	struct host_cmd_handler_args read_args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_CONSOLE_READ, ver, response);
	char *msg1_start;
	char *msg2_start;
	char *msg3_start;

	/* Set up host command parameters */
	if (ver == 1) {
		read_args.params = &params;
		read_args.params_size = sizeof(params);
		params.subcmd = CONSOLE_READ_NEXT;
	}

	read_args.response_size = 0;
	zassert_equal(EC_RES_SUCCESS, host_command_process(&read_args));
	zassert_equal('\0', response[read_args.response_size],
		      "Last byte of response is not '\\0' (got 0x%x)",
		      response[read_args.response_size]);

	/*
	 * Whole buffer until snapshot should be in response, check if it ends
	 * with message 1 which should start at the end of response excluding
	 * NULL char.
	 */
	msg1_start = response + read_args.response_size - 1 - MSG_LEN(msg1);
	zassert_mem_equal(msg1, msg1_start, MSG_LEN(msg1),
			  "expected \"%s\", got \"%.*s\"", msg1, MSG_LEN(msg1),
			  msg1_start);

	/* Set new snapshot which should include message 2 */
	zassert_equal(EC_RES_SUCCESS, ec_cmd_console_snapshot(NULL));

	read_args.response_size = 0;
	zassert_equal(EC_RES_SUCCESS, host_command_process(&read_args));
	zassert_equal('\0', response[read_args.response_size],
		      "Last byte of response is not '\\0' (got 0x%x)",
		      response[read_args.response_size]);

	/*
	 * Whole buffer should be in response, check if it ends with both
	 * messages. Message 2 should start at the end of response excluding
	 * NULL char.
	 */
	msg2_start = response + read_args.response_size - 1 - MSG_LEN(msg2);
	msg1_start = msg2_start - MSG_LEN(msg1);
	zassert_mem_equal(msg2, msg2_start, MSG_LEN(msg2),
			  "expected \"%s\", got \"%.*s\"", msg2, MSG_LEN(msg2),
			  msg2_start);
	zassert_mem_equal(msg1, msg1_start, MSG_LEN(msg1),
			  "expected \"%s\", got \"%.*s\"", msg1, MSG_LEN(msg1),
			  msg1_start);

	/* Append third message, but use Zephyr's logging subsystem. */
	LOG_RAW(LOG_TEST_MSG);

	/* Check read next without new snapshot, no data should be read */
	read_args.response_size = 0;
	zassert_equal(EC_RES_SUCCESS, host_command_process(&read_args));
	zassert_equal(0, read_args.response_size,
		      "expected message length 0, got %d",
		      read_args.response_size);

	/* Set new snapshot which should include message 3 */
	zassert_equal(EC_RES_SUCCESS, ec_cmd_console_snapshot(NULL));

	read_args.response_size = 0;
	zassert_equal(EC_RES_SUCCESS, host_command_process(&read_args));
	zassert_equal('\0', response[read_args.response_size],
		      "Last byte of response is not '\\0' (got 0x%x)",
		      response[read_args.response_size]);

	msg3_start = response + read_args.response_size - 1 - MSG_LEN(msg3);
	msg2_start = msg3_start - MSG_LEN(msg2);
	msg1_start = msg2_start - MSG_LEN(msg1);
	zassert_mem_equal(msg3, msg3_start, MSG_LEN(msg3),
			  "expected \"%s\", got \"%.*s\"", msg3, MSG_LEN(msg3),
			  msg3_start);
	zassert_mem_equal(msg2, msg2_start, MSG_LEN(msg2),
			  "expected \"%s\", got \"%.*s\"", msg2, MSG_LEN(msg2),
			  msg2_start);
	zassert_mem_equal(msg1, msg1_start, MSG_LEN(msg1),
			  "expected \"%s\", got \"%.*s\"", msg1, MSG_LEN(msg1),
			  msg1_start);
}

ZTEST_USER(uart_hostcmd, test_uart_hc_read_next_v0)
{
	test_uart_hc_read_next(0);
}

ZTEST_USER(uart_hostcmd, test_uart_hc_read_next_v1)
{
	test_uart_hc_read_next(1);
}

/** Test if read recent variant of console read host command is working */
ZTEST_USER(uart_hostcmd, test_uart_hc_read_recent_v1)
{
	/* Should be able to read whole buffer in one command */
	char response[CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_BUF_SIZE + 1];
	struct ec_params_console_read_v1 params;
	struct host_cmd_handler_args read_args =
		BUILD_HOST_COMMAND(EC_CMD_CONSOLE_READ, 1, response, params);

	params.subcmd = CONSOLE_READ_RECENT;

	/* Only message 1 which is between two last snapshots should be read */
	read_args.response_size = 0;
	zassert_equal(EC_RES_SUCCESS, host_command_process(&read_args));
	zassert_equal('\0', response[read_args.response_size],
		      "Last byte of response is not '\\0' (got 0x%x)",
		      response[read_args.response_size]);
	/* Account additional NULL char at the end */
	zassert_equal(MSG_LEN(msg1) + 1, read_args.response_size,
		      "expected message length %d, got %d", MSG_LEN(msg1) + 1,
		      read_args.response_size);
	zassert_mem_equal(msg1, response, MSG_LEN(msg1),
			  "expected \"%s\", got \"%.*s\"", msg1, MSG_LEN(msg1),
			  response);

	/* Set new snapshot after second message */
	zassert_equal(EC_RES_SUCCESS, ec_cmd_console_snapshot(NULL));

	/* Only message between two last snapshots should be read */
	read_args.response_size = 0;
	zassert_equal(EC_RES_SUCCESS, host_command_process(&read_args));
	zassert_equal('\0', response[read_args.response_size],
		      "Last byte of response is not '\\0' (got 0x%x)",
		      response[read_args.response_size]);
	/* Account additional NULL char at the end */
	zassert_equal(MSG_LEN(msg2) + 1, read_args.response_size,
		      "expected message length %d, got %d", MSG_LEN(msg2) + 1,
		      read_args.response_size);
	zassert_mem_equal(msg2, response, MSG_LEN(msg2),
			  "expected \"%s\", got \"%.*s\"", msg2, MSG_LEN(msg2),
			  response);

	/* Append third message, but use Zephyr's logging subsystem. */
	LOG_RAW(LOG_TEST_MSG);

	/* Check that message is not read without setting snapshot */
	read_args.response_size = 0;
	zassert_equal(EC_RES_SUCCESS, host_command_process(&read_args));
	zassert_equal(0, read_args.response_size,
		      "expected message length 0, got %d",
		      read_args.response_size);

	/* Set new snapshot */
	zassert_equal(EC_RES_SUCCESS, ec_cmd_console_snapshot(NULL));

	/* This time only third message should be read */
	read_args.response_size = 0;
	zassert_equal(EC_RES_SUCCESS, host_command_process(&read_args));
	zassert_equal('\0', response[read_args.response_size],
		      "Last byte of response is not '\\0' (got 0x%x)",
		      response[read_args.response_size]);
	/* Account additional NULL char at the end */
	zassert_equal(MSG_LEN(msg3) + 1, read_args.response_size,
		      "expected message length %d, got %d", MSG_LEN(msg3) + 1,
		      read_args.response_size);
	zassert_mem_equal(msg3, response, MSG_LEN(msg3),
			  "expected \"%s\", got \"%.*s\"", msg3, MSG_LEN(msg3),
			  response);
}

ZTEST_SUITE(uart_hostcmd, predicate_post_main, NULL,
	    setup_snapshots_and_messages, NULL, NULL);
