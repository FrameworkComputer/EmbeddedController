/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "console.h"
#include "keyboard_8042.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"

#include <string.h>

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

ZTEST(keyboard_8042, test_console_cmd__typematic__status)
{
	const char *outbuffer;
	size_t buffer_size;

	/* Set a typematic scan code to verify */
	const uint8_t scan_code[] = { 0x01, 0x02, 0x03 };

	set_typematic_key(scan_code, ARRAY_SIZE(scan_code));

	/* With no args, print current state */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "8042 typematic"));
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	/* Check for some expected lines based off default typematic state */
	zassert_true(buffer_size > 0);
	zassert_ok(!strstr(outbuffer, "From host:   0x2b"));
	zassert_ok(!strstr(outbuffer, "First delay: 500 ms"));
	zassert_ok(!strstr(outbuffer, "Inter delay:  91 ms"));
	zassert_ok(
		!strstr(outbuffer, "Repeat scan code: {0x01, 0x02, 0x03, }"));
}

ZTEST(keyboard_8042, test_console_cmd__typematic__set_delays)
{
	const char *outbuffer;
	size_t buffer_size;

	/* Set first delay and inter delay */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "8042 typematic 123 456"));
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	/* Check for some expected lines */
	zassert_true(buffer_size > 0);
	zassert_ok(!strstr(outbuffer, "First delay: 123 ms"));
	zassert_ok(!strstr(outbuffer, "Inter delay: 456 ms"));
}

ZTEST(keyboard_8042, test_console_cmd__codeset__set_codeset1)
{
	const char *outbuffer;
	size_t buffer_size;

	/* Switch to codeset 1 and verify output */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "8042 codeset 1"));
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	zassert_true(buffer_size > 0);
	zassert_ok(!strstr(outbuffer, "Set: 1"));
}

ZTEST(keyboard_8042, test_console_cmd__codeset__set_invalid)
{
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "8042 codeset 999"));
}

ZTEST(keyboard_8042, test_console_cmd__ram__writeread)
{
	const char *outbuffer;
	size_t buffer_size;

	/* Write a byte and verify the readback in console output */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "8042 ctrlram 0x1f 0xaa"));
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	zassert_true(buffer_size > 0);
	zassert_ok(!strstr(outbuffer, "31 = 0xaa"));
}

ZTEST(keyboard_8042, test_console_cmd__ram__invalid)
{
	/* Missing args */
	zassert_equal(EC_ERROR_PARAM_COUNT,
		      shell_execute_cmd(get_ec_shell(), "8042 ctrlram"));

	/* Address out of bounds */
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "8042 ctrlram 9999"));
}

ZTEST(keyboard_8042, test_console_cmd__enable__true)
{
	const char *outbuffer;
	size_t buffer_size;

	/* Enable the keyboard and verify in console output */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "8042 kbd y"));
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	zassert_true(buffer_size > 0);
	zassert_ok(!strstr(outbuffer, "Enabled: 1"));
}

ZTEST(keyboard_8042, test_console_cmd__enable__invalid)
{
	/* Non-bool arg */
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "8042 kbd abc"));
}

ZTEST(keyboard_8042, test_console_cmd__internal)
{
	const char *outbuffer;
	size_t buffer_size;

	uint8_t resend_command[] = { 7, 8, 9 };

	test_keyboard_8042_set_resend_command(resend_command,
					      ARRAY_SIZE(resend_command));

	/* Dump the internal state of the keyboard driver */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "8042 internal"));
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	zassert_true(buffer_size > 0);
	zassert_ok(!strstr(outbuffer, "keyboard_enabled=0"));
	zassert_ok(!strstr(outbuffer, "i8042_keyboard_irq_enabled=0"));
	zassert_ok(!strstr(outbuffer, "i8042_aux_irq_enabled=0"));
	zassert_ok(!strstr(outbuffer, "keyboard_enabled=0"));
	zassert_ok(!strstr(outbuffer, "keystroke_enabled=0"));
	zassert_ok(!strstr(outbuffer, "aux_chan_enabled=0"));
	zassert_ok(!strstr(outbuffer, "controller_ram_address=0x00"));
	zassert_ok(!strstr(outbuffer, "resend_command[]={0x07, 0x08, 0x09, }"));
	zassert_ok(!strstr(outbuffer, "A20_status=0"));
}

ZTEST(keyboard_8042, test_console_cmd__invalid)
{
	/* Non-existent subcommand */
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "8042 foobar"));
}

ZTEST(keyboard_8042, test_console_cmd__all)
{
	const char *outbuffer;
	size_t buffer_size;

	/* Run all the subcommands */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "8042"));
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	/* Just look for the headers since we already tested the individual
	 * subcommands
	 */

	zassert_true(buffer_size > 0);
	zassert_ok(!strstr(outbuffer, "- Typematic:"));
	zassert_ok(!strstr(outbuffer, "- Codeset:"));
	zassert_ok(!strstr(outbuffer, "- Control RAM:"));
	zassert_ok(!strstr(outbuffer, "- Keyboard:"));
	zassert_ok(!strstr(outbuffer, "- Internal:"));
}

static void reset(void *fixture)
{
	ARG_UNUSED(fixture);

	test_keyboard_8042_reset();
}

ZTEST_SUITE(keyboard_8042, drivers_predicate_post_main, NULL, reset, reset,
	    NULL);
