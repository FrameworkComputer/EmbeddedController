/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test console editing and history.
 */

#include "common.h"
#include "console.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

static int cmd_1_call_cnt;
static int cmd_2_call_cnt;

static int command_test_1(int argc, char **argv)
{
	cmd_1_call_cnt++;
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(test1, command_test_1, NULL, NULL, NULL);

static int command_test_2(int argc, char **argv)
{
	cmd_2_call_cnt++;
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(test2, command_test_2, NULL, NULL, NULL);

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
	static char seq[4] = {0x1B, '[', 0, 0};
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
	static char seq[2] = {0, 0};
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
		++s1;
		++s2;
	} while (*s1 || *s2);

	return 0;
}

/*****************************************************************************/
/* Tests */

static int test_backspace(void)
{
	cmd_1_call_cnt = 0;
	UART_INJECT("testx\b1\n");
	msleep(30);
	TEST_CHECK(cmd_1_call_cnt == 1);
}

static int test_insert_char(void)
{
	cmd_1_call_cnt = 0;
	UART_INJECT("tet1");
	arrow_key(ARROW_LEFT, 2);
	UART_INJECT("s\n");
	msleep(30);
	TEST_CHECK(cmd_1_call_cnt == 1);
}

static int test_delete_char(void)
{
	cmd_1_call_cnt = 0;
	UART_INJECT("testt1");
	arrow_key(ARROW_LEFT, 1);
	UART_INJECT("\b\n");
	msleep(30);
	TEST_CHECK(cmd_1_call_cnt == 1);
}

static int test_insert_delete_char(void)
{
	cmd_1_call_cnt = 0;
	UART_INJECT("txet1");
	arrow_key(ARROW_LEFT, 4);
	delete_key();
	arrow_key(ARROW_RIGHT, 1);
	UART_INJECT("s\n");
	msleep(30);
	TEST_CHECK(cmd_1_call_cnt == 1);
}

static int test_home_end_key(void)
{
	cmd_1_call_cnt = 0;
	UART_INJECT("est");
	home_key();
	UART_INJECT("t");
	end_key();
	UART_INJECT("1\n");
	msleep(30);
	TEST_CHECK(cmd_1_call_cnt == 1);
}

static int test_ctrl_k(void)
{
	cmd_1_call_cnt = 0;
	UART_INJECT("test123");
	arrow_key(ARROW_LEFT, 2);
	ctrl_key('K');
	UART_INJECT("\n");
	msleep(30);
	TEST_CHECK(cmd_1_call_cnt == 1);
}

static int test_history_up(void)
{
	cmd_1_call_cnt = 0;
	UART_INJECT("test1\n");
	msleep(30);
	arrow_key(ARROW_UP, 1);
	UART_INJECT("\n");
	msleep(30);
	TEST_CHECK(cmd_1_call_cnt == 2);
}

static int test_history_up_up(void)
{
	cmd_1_call_cnt = 0;
	cmd_2_call_cnt = 0;
	UART_INJECT("test1\n");
	msleep(30);
	UART_INJECT("test2\n");
	msleep(30);
	arrow_key(ARROW_UP, 2);
	UART_INJECT("\n");
	msleep(30);
	TEST_CHECK(cmd_1_call_cnt == 2 && cmd_2_call_cnt == 1);
}

static int test_history_up_up_down(void)
{
	cmd_1_call_cnt = 0;
	cmd_2_call_cnt = 0;
	UART_INJECT("test1\n");
	msleep(30);
	UART_INJECT("test2\n");
	msleep(30);
	arrow_key(ARROW_UP, 2);
	arrow_key(ARROW_DOWN, 1);
	UART_INJECT("\n");
	msleep(30);
	TEST_CHECK(cmd_1_call_cnt == 1 && cmd_2_call_cnt == 2);
}

static int test_history_edit(void)
{
	cmd_1_call_cnt = 0;
	cmd_2_call_cnt = 0;
	UART_INJECT("test1\n");
	msleep(30);
	arrow_key(ARROW_UP, 1);
	UART_INJECT("\b2\n");
	msleep(30);
	TEST_CHECK(cmd_1_call_cnt == 1 && cmd_2_call_cnt == 1);
}

static int test_history_stash(void)
{
	cmd_1_call_cnt = 0;
	cmd_2_call_cnt = 0;
	UART_INJECT("test1\n");
	msleep(30);
	UART_INJECT("test");
	arrow_key(ARROW_UP, 1);
	arrow_key(ARROW_DOWN, 1);
	UART_INJECT("2\n");
	msleep(30);
	TEST_CHECK(cmd_1_call_cnt == 1 && cmd_2_call_cnt == 1);
}

static int test_history_list(void)
{
	const char *exp_output = "history\n" /* Input command */
				 "test3\n"   /* Output 4 last commands */
				 "test4\n"
				 "test5\n"
				 "history\n"
				 "> ";

	UART_INJECT("test1\n");
	UART_INJECT("test2\n");
	UART_INJECT("test3\n");
	UART_INJECT("test4\n");
	UART_INJECT("test5\n");
	msleep(30);
	test_capture_console(1);
	UART_INJECT("history\n");
	msleep(30);
	test_capture_console(0);
	TEST_ASSERT(compare_multiline_string(test_get_captured_console(),
					     exp_output) == 0);

	return EC_SUCCESS;
}

void run_test(void)
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

	test_print_result();
}
