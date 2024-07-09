/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "builtin/stdio.h"
#include "console.h"
#include "ec_commands.h"
#include "test/drivers/test_state.h"
#include "uart.h"

#include <zephyr/kernel.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

ZTEST_USER(console, test_printf_overflow)
{
	char buffer[10];

	zassert_equal(-EC_ERROR_OVERFLOW,
		      crec_snprintf(buffer, 4, "1234567890"), NULL);
	zassert_equal(0, strcmp(buffer, "123"), "got '%s'", buffer);
	zassert_equal(-EC_ERROR_OVERFLOW,
		      crec_snprintf(buffer, 4, "%%%%%%%%%%"), NULL);
	zassert_equal(0, strcmp(buffer, "%%%"), "got '%s'", buffer);
}

/* This test is identical to test_buf_notify_null in
 * test/console_edit.c. Please keep them in sync to verify that
 * uart_console_read_buffer works identically in legacy EC and zephyr.
 */
ZTEST_USER(console, test_buf_notify_null)
{
	char buffer[100];
	uint16_t write_count;
	size_t consumed_count;

	/* Flush the console buffer before we start. */
	zassert_ok(uart_console_read_buffer_init(), NULL);

	/* Write a nul char to the buffer. */
	consumed_count = console_buf_notify_chars("ab\0c", 4);

	/* Check if all bytes were consumed by console buffer */
	zassert_equal(consumed_count, 4, "got %d", consumed_count);

	/* Check if the nul is present in the buffer. */
	zassert_ok(uart_console_read_buffer_init(), NULL);
	zassert_ok(uart_console_read_buffer(CONSOLE_READ_RECENT, buffer,
					    sizeof(buffer), &write_count),
		   NULL);
	zassert_equal(0, strncmp(buffer, "abc", 4), "got '%s'", buffer);
	zassert_equal(write_count, 4, "got %d", write_count);
}

ZTEST_USER(console, test_console_read_buffer_invalid_type)
{
	char buffer[100];
	uint16_t write_count;
	uint8_t invalid_type = CONSOLE_READ_RECENT + 1;

	/* Flush the console buffer before we start. */
	zassert_ok(uart_console_read_buffer_init(), NULL);

	zassert_equal(EC_RES_INVALID_PARAM,
		      uart_console_read_buffer(invalid_type, buffer,
					       sizeof(buffer), &write_count),
		      NULL);
}

ZTEST_USER(console, test_console_read_buffer_size_zero)
{
	char buffer[100];
	uint16_t write_count;

	/* Flush the console buffer before we start. */
	zassert_ok(uart_console_read_buffer_init(), NULL);

	zassert_equal(EC_RES_INVALID_PARAM,
		      uart_console_read_buffer(CONSOLE_READ_RECENT, buffer, 0,
					       &write_count),
		      NULL);
}

ZTEST_USER(console, test_uart_buffer_full)
{
	zassert_false(uart_buffer_full(), NULL);
}

static const char *large_string =
	"This is a very long string, it will cause a buffer flush at "
	"some point while printing to the shell. Long long text. Blah "
	"blah. Long long text. Blah blah. Long long text. Blah blah.";
ZTEST_USER(console, test_shell_fprintf_full)
{
	const struct shell *shell_zephyr = get_ec_shell();
	const char *outbuffer;
	size_t buffer_size;

	zassert_true(strlen(large_string) >=
			     shell_zephyr->fprintf_ctx->buffer_size,
		     "large_string is too short, fix test.");

	shell_backend_dummy_clear_output(shell_zephyr);
	shell_fprintf(shell_zephyr, SHELL_NORMAL, "%s", large_string);

	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_true(strncmp(outbuffer, large_string, strlen(large_string)) ==
			     0,
		     "Invalid console output %s", outbuffer);
}

ZTEST_USER(console, test_cprint_too_big)
{
	zassert_true(strlen(large_string) >= CONFIG_SHELL_PRINTF_BUFF_SIZE,
		     "buffer is too short, fix test.");

	zassert_equal(cprintf(CC_COMMAND, "%s", large_string),
		      -EC_ERROR_OVERFLOW, NULL);
}

ZTEST_USER(console, test_cmd_chan_invalid_mask)
{
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "chan foobar"));
}

ZTEST_USER(console, test_cmd_chan_set)
{
	char cmd[100];

	zassert_true(crec_snprintf(cmd, sizeof(cmd), "chan %d",
				   CC_MASK(CC_ACCEL)) > 0);
	zassert_ok(shell_execute_cmd(get_ec_shell(), cmd));

	zassert_false(console_channel_is_disabled(CC_COMMAND));
	zassert_false(console_channel_is_disabled(CC_ACCEL));
	zassert_true(console_channel_is_disabled(CC_CHARGER));
}

ZTEST_USER(console, test_cmd_chan_by_name)
{
	const char name[] = "charger";
	char cmd[100];

	console_channel_enable(name);

	/* Toggle 'charger' off */
	zassert_true(crec_snprintf(cmd, sizeof(cmd), "chan %s", name) > 0,
		     "Failed to compose chan %s command.", name);
	zassert_ok(shell_execute_cmd(get_ec_shell(), cmd),
		   "Failed to execute chan %s command.", name);
	zassert_true(console_channel_is_disabled(CC_CHARGER),
		     "Failed to enable %s channel.", name);

	/* Toggle 'charger' on */
	zassert_true(crec_snprintf(cmd, sizeof(cmd), "chan %s", name) > 0,
		     "Failed to compose chan %s command.", name);
	zassert_ok(shell_execute_cmd(get_ec_shell(), cmd),
		   "Failed to execute chan %s command.", name);
	zassert_false(console_channel_is_disabled(CC_CHARGER),
		      "Failed to disable %s channel.", name);
}

ZTEST_USER(console, test_cmd_chan_show)
{
	const struct shell *shell_zephyr = get_ec_shell();
	const char *outbuffer;
	size_t buffer_size;
	char cmd[100];

	zassert_true(crec_snprintf(cmd, sizeof(cmd), "chan %d",
				   CC_MASK(CC_ACCEL)) > 0);
	zassert_ok(shell_execute_cmd(get_ec_shell(), cmd));
	shell_backend_dummy_clear_output(shell_zephyr);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "chan"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);

	zassert_true(
		strstr(outbuffer,
		       "\r\n # Mask     E Channel\r\n 0 00000001 * command\r\n"
		       " 1 00000002 * accel\r\n 2 00000004   charger\r\n") !=
			NULL,
		"Invalid console output %s", outbuffer);
}

ZTEST_USER(console, test_cmd_chan_save_restore)
{
	char cmd[100];

	zassert_true(crec_snprintf(cmd, sizeof(cmd), "chan %d",
				   CC_MASK(CC_ACCEL)) > 0);
	zassert_ok(shell_execute_cmd(get_ec_shell(), cmd));

	zassert_false(console_channel_is_disabled(CC_COMMAND));
	zassert_false(console_channel_is_disabled(CC_ACCEL));
	zassert_true(console_channel_is_disabled(CC_CHARGER));

	zassert_ok(shell_execute_cmd(get_ec_shell(), "chan save"));
	zassert_true(crec_snprintf(cmd, sizeof(cmd), "chan %d",
				   CC_MASK(CC_ACCEL) | CC_MASK(CC_CHARGER)) >
		     0);
	zassert_ok(shell_execute_cmd(get_ec_shell(), cmd));

	zassert_false(console_channel_is_disabled(CC_COMMAND));
	zassert_false(console_channel_is_disabled(CC_ACCEL));
	zassert_false(console_channel_is_disabled(CC_CHARGER));

	zassert_ok(shell_execute_cmd(get_ec_shell(), "chan restore"));

	zassert_false(console_channel_is_disabled(CC_COMMAND));
	zassert_false(console_channel_is_disabled(CC_ACCEL));
	zassert_true(console_channel_is_disabled(CC_CHARGER));
}

ZTEST_SUITE(console, drivers_predicate_post_main, NULL, NULL, NULL, NULL);

ZTEST_USER(console_pre, test_cmd_chan_save_restore)
{
	/* These are not mentioned in ec-console in native_sim.overlay. */
	zassert_false(console_channel_is_disabled(CC_COMMAND));
	zassert_false(console_channel_is_disabled(CC_ACCEL));
	zassert_false(console_channel_is_disabled(CC_CHARGER));
	/* These are disabled in ec-console in native_sim.overlay. */
	zassert_true(console_channel_is_disabled(CC_EVENTS));
	zassert_true(console_channel_is_disabled(CC_LPC));
	zassert_true(console_channel_is_disabled(CC_HOSTCMD));

	/* Disable an invalid channel, and verify nothing changed. */
	console_channel_disable("not_a_valid_channel");

	zassert_false(console_channel_is_disabled(CC_COMMAND));
	zassert_false(console_channel_is_disabled(CC_ACCEL));
	zassert_false(console_channel_is_disabled(CC_CHARGER));
	zassert_true(console_channel_is_disabled(CC_EVENTS));
	zassert_true(console_channel_is_disabled(CC_LPC));
	zassert_true(console_channel_is_disabled(CC_HOSTCMD));
}

ZTEST_SUITE(console_pre, drivers_predicate_pre_main, NULL, NULL, NULL, NULL);
