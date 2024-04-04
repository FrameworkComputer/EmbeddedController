/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"

#include <stdio.h>

#include <zephyr/ztest.h>

static int is_locked;

int system_is_locked(void)
{
	return is_locked;
}

ZTEST_SUITE(restricted_console, NULL, NULL, NULL, NULL, NULL);

ZTEST(restricted_console, test_command_mem_dump)
{
	int rv;
	/* This word will be read by the md command. */
	const volatile uint32_t valid_word = 0x1badd00d;
	/* Compose the md console command to read |valid_word|. */
	char console_input[] = "md 0x01234567";

	snprintf(console_input, sizeof(console_input), "md %p", &valid_word);

	is_locked = 0;
	rv = shell_execute_cmd(get_ec_shell(), console_input);
	zassert_equal(rv, EC_SUCCESS);

	is_locked = 1;
	rv = shell_execute_cmd(get_ec_shell(), console_input);
	zassert_equal(rv, EC_ERROR_ACCESS_DENIED);
}

ZTEST(restricted_console, test_command_read_write_word)
{
	int rv;
	const uint32_t old_value = 0x1badd00d;
	/* This word will be read/written by the rw command. */
	volatile uint32_t valid_word = old_value;
	/* Compose the rw console command to write |valid_word| with a value
	 * of 5. */
	char console_input[] = "rw 0x01234567 0x05";
	const uint32_t new_value = 0x05;

	snprintf(console_input, sizeof(console_input), "rw %p 0x%02x",
		 &valid_word, new_value);

	is_locked = 0;
	rv = shell_execute_cmd(get_ec_shell(), console_input);
	zassert_equal(rv, EC_SUCCESS);
	zassert_equal(new_value, valid_word);

	is_locked = 1;
	/* Reset valid word */
	valid_word = old_value;
	rv = shell_execute_cmd(get_ec_shell(), console_input);
	zassert_equal(rv, EC_ERROR_ACCESS_DENIED);
	zassert_equal(old_value, valid_word);
}
