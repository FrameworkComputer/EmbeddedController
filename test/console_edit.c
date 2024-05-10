/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test console editing and history.
 */

#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "test_util.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

static int cmd_1_call_cnt;
static int cmd_2_call_cnt;

static int command_test_1(int argc, const char **argv)
{
	cmd_1_call_cnt++;
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(test1, command_test_1, NULL, NULL);

static int command_test_2(int argc, const char **argv)
{
	cmd_2_call_cnt++;
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(test2, command_test_2, NULL, NULL);

/*****************************************************************************/
/* Test utilities */

enum arrow_key_t {
	ARROW_UP = 0,
	ARROW_DOWN,
	ARROW_RIGHT,
	ARROW_LEFT,
};

static void arrow_key(enum arrow_key_t k, int repeat)
{
	static char seq[4] = { 0x1B, '[', 0, 0 };
	seq[2] = 'A' + k;
	while (repeat--)
		UART_INJECT(seq);
}

static void delete_key(void)
{
	UART_INJECT("\x1b[3~");
}

static void home_key(void)
{
	UART_INJECT("\x1b[1~");
}

static void end_key(void)
{
	UART_INJECT("\x1bOF");
}

static void ctrl_key(char c)
{
	static char seq[2] = { 0, 0 };
	seq[0] = c - '@';
	UART_INJECT(seq);
}

/*
 * Helper function to compare multiline strings. When comparing, CR's are
 * ignored.
 */
static int compare_multiline_string(const char *s1, const char *s2)
{
	do {
		while (*s1 == '\r')
			++s1;
		while (*s2 == '\r')
			++s2;
		if (*s1 != *s2)
			return 1;
		if (*s1 == 0 && *s2 == 0)
			break;
		++s1;
		++s2;
	} while (1);

	return 0;
}

/*****************************************************************************/
/* Tests */

static int test_backspace(void)
{
	cmd_1_call_cnt = 0;
	UART_INJECT("testx\b1\n");
	crec_msleep(30);
	TEST_ASSERT(cmd_1_call_cnt == 1);
	return EC_SUCCESS;
}

static int test_insert_char(void)
{
	cmd_1_call_cnt = 0;
	UART_INJECT("tet1");
	arrow_key(ARROW_LEFT, 2);
	UART_INJECT("s\n");
	crec_msleep(30);
	TEST_ASSERT(cmd_1_call_cnt == 1);
	return EC_SUCCESS;
}

static int test_delete_char(void)
{
	cmd_1_call_cnt = 0;
	UART_INJECT("testt1");
	arrow_key(ARROW_LEFT, 1);
	UART_INJECT("\b\n");
	crec_msleep(30);
	TEST_ASSERT(cmd_1_call_cnt == 1);
	return EC_SUCCESS;
}

static int test_insert_delete_char(void)
{
	cmd_1_call_cnt = 0;
	UART_INJECT("txet1");
	arrow_key(ARROW_LEFT, 4);
	delete_key();
	arrow_key(ARROW_RIGHT, 1);
	UART_INJECT("s\n");
	crec_msleep(30);
	TEST_ASSERT(cmd_1_call_cnt == 1);
	return EC_SUCCESS;
}

static int test_home_end_key(void)
{
	cmd_1_call_cnt = 0;
	UART_INJECT("est");
	home_key();
	UART_INJECT("t");
	end_key();
	UART_INJECT("1\n");
	crec_msleep(30);
	TEST_ASSERT(cmd_1_call_cnt == 1);
	return EC_SUCCESS;
}

static int test_ctrl_k(void)
{
	cmd_1_call_cnt = 0;
	UART_INJECT("test123");
	arrow_key(ARROW_LEFT, 2);
	ctrl_key('K');
	UART_INJECT("\n");
	crec_msleep(30);
	TEST_ASSERT(cmd_1_call_cnt == 1);
	return EC_SUCCESS;
}

static int test_history_up(void)
{
	cmd_1_call_cnt = 0;
	UART_INJECT("test1\n");
	crec_msleep(30);
	arrow_key(ARROW_UP, 1);
	UART_INJECT("\n");
	crec_msleep(30);
	TEST_ASSERT(cmd_1_call_cnt == 2);
	return EC_SUCCESS;
}

static int test_history_up_up(void)
{
	cmd_1_call_cnt = 0;
	cmd_2_call_cnt = 0;
	UART_INJECT("test1\n");
	crec_msleep(30);
	UART_INJECT("test2\n");
	crec_msleep(30);
	arrow_key(ARROW_UP, 2);
	UART_INJECT("\n");
	crec_msleep(30);
	TEST_ASSERT(cmd_1_call_cnt == 2 && cmd_2_call_cnt == 1);
	return EC_SUCCESS;
}

static int test_history_up_up_down(void)
{
	cmd_1_call_cnt = 0;
	cmd_2_call_cnt = 0;
	UART_INJECT("test1\n");
	crec_msleep(30);
	UART_INJECT("test2\n");
	crec_msleep(30);
	arrow_key(ARROW_UP, 2);
	arrow_key(ARROW_DOWN, 1);
	UART_INJECT("\n");
	crec_msleep(30);
	TEST_ASSERT(cmd_1_call_cnt == 1 && cmd_2_call_cnt == 2);
	return EC_SUCCESS;
}

static int test_history_edit(void)
{
	cmd_1_call_cnt = 0;
	cmd_2_call_cnt = 0;
	UART_INJECT("test1\n");
	crec_msleep(30);
	arrow_key(ARROW_UP, 1);
	UART_INJECT("\b2\n");
	crec_msleep(30);
	TEST_ASSERT(cmd_1_call_cnt == 1 && cmd_2_call_cnt == 1);
	return EC_SUCCESS;
}

static int test_history_stash(void)
{
	cmd_1_call_cnt = 0;
	cmd_2_call_cnt = 0;
	UART_INJECT("test1\n");
	crec_msleep(30);
	UART_INJECT("test");
	arrow_key(ARROW_UP, 1);
	arrow_key(ARROW_DOWN, 1);
	UART_INJECT("2\n");
	crec_msleep(30);
	TEST_ASSERT(cmd_1_call_cnt == 1 && cmd_2_call_cnt == 1);
	return EC_SUCCESS;
}

static int test_history_list(void)
{
	const char *exp_output = "history\n" /* Input command */
				 "test3\n" /* Output 4 last commands */
				 "test4\n"
				 "test5\n"
				 "history\n"
				 "> ";

	UART_INJECT("test1\n");
	UART_INJECT("test2\n");
	UART_INJECT("test3\n");
	UART_INJECT("test4\n");
	UART_INJECT("test5\n");
	crec_msleep(30);
	test_capture_console(1);
	UART_INJECT("history\n");
	crec_msleep(30);
	test_capture_console(0);
	TEST_ASSERT(compare_multiline_string(test_get_captured_console(),
					     exp_output) == 0);

	return EC_SUCCESS;
}

static int test_output_channel(void)
{
	UART_INJECT("chan save\n");
	crec_msleep(30);
	UART_INJECT("chan 0\n");
	crec_msleep(30);
	test_capture_console(1);
	cprintf(CC_SYSTEM, "shouldn't see this\n");
	cputs(CC_TASK, "shouldn't see this either\n");
	cflush();
	test_capture_console(0);
	TEST_ASSERT(compare_multiline_string(test_get_captured_console(), "") ==
		    0);
	UART_INJECT("chan restore\n");
	crec_msleep(30);
	test_capture_console(1);
	cprintf(CC_SYSTEM, "see me\n");
	cputs(CC_TASK, "me as well\n");
	cflush();
	test_capture_console(0);
	TEST_ASSERT(compare_multiline_string(test_get_captured_console(),
					     "see me\nme as well\n") == 0);

	return EC_SUCCESS;
}

/* This test is identical to console::buf_notify_null in
 * zephyr/test/drivers/default/src/console.c. Please keep them in sync to
 * verify that uart_console_read_buffer works identically in legacy EC and
 * zephyr.
 */
static int test_buf_notify_null(void)
{
	char buffer[100];
	uint16_t write_count;

	/* Flush the console buffer before we start. */
	TEST_ASSERT(uart_console_read_buffer_init() == 0);

	/* Write a nul char to the buffer. */
	cprintf(CC_SYSTEM, "ab%cc", 0);
	cflush();

	/* Check if the nul is present in the buffer. */
	TEST_ASSERT(uart_console_read_buffer_init() == 0);
	TEST_ASSERT(uart_console_read_buffer(CONSOLE_READ_RECENT, buffer,
					     sizeof(buffer),
					     &write_count) == 0);
	TEST_ASSERT(strncmp(buffer, "abc", 4) == 0);
	TEST_EQ(write_count, 4, "%d");

	return EC_SUCCESS;
}

static const char *large_string =
	"This is a very long string, it will cause a buffer flush at "
	"some point while printing to the shell. Long long text. Blah "
	"blah. Long long text. Blah blah. Long long text. Blah blah."
	"This is a very long string, it will cause a buffer flush at "
	"some point while printing to the shell. Long long text. Blah "
	"blah. Long long text. Blah blah. Long long text. Blah blah."
	"This is a very long string, it will cause a buffer flush at "
	"some point while printing to the shell. Long long text. Blah "
	"blah. Long long text. Blah blah. Long long text. Blah blah.";
static int test_cprints_overflow(void)
{
	TEST_GE(strlen(large_string), (size_t)CONFIG_UART_TX_BUF_SIZE, "%ld");

	TEST_NE(cprints(CC_SYSTEM, large_string), 0, "%d");

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_backspace);
	RUN_TEST(test_insert_char);
	RUN_TEST(test_delete_char);
	RUN_TEST(test_insert_delete_char);
	RUN_TEST(test_home_end_key);
	RUN_TEST(test_ctrl_k);
	RUN_TEST(test_history_up);
	RUN_TEST(test_history_up_up);
	RUN_TEST(test_history_up_up_down);
	RUN_TEST(test_history_edit);
	RUN_TEST(test_history_stash);
	RUN_TEST(test_history_list);
	RUN_TEST(test_output_channel);
	RUN_TEST(test_buf_notify_null);
	RUN_TEST(test_cprints_overflow);

	test_print_result();
}
