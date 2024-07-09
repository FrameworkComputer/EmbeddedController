/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Tests for keyboard MKBP protocol
 */

#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "host_command.h"
#include "keyboard_mkbp.h"
#include "keyboard_protocol.h"
#include "keyboard_scan.h"
#include "test_util.h"
#include "util.h"

static uint8_t state[KEYBOARD_COLS_MAX];
static int ec_int_level;

static const char *action[2] = { "release", "press" };

/*****************************************************************************/
/* Mock functions */

void host_send_response(struct host_cmd_handler_args *args)
{
	/* Do nothing */
}

void gpio_set_level(enum gpio_signal signal, int level)
{
	if (signal == GPIO_EC_INT_L)
		ec_int_level = !!level;
}

int lid_is_open(void)
{
	return 1;
}

/*****************************************************************************/
/* Test utilities */

#define FIFO_EMPTY() (ec_int_level == 1)
#define FIFO_NOT_EMPTY() (ec_int_level == 0)

void clear_state(void)
{
	memset(state, 0xff, KEYBOARD_COLS_MAX);
}

void set_state(int c, int r, int pressed)
{
	uint8_t mask = (1 << r);

	if (pressed)
		state[c] &= ~mask;
	else
		state[c] |= mask;
}

int press_key(int c, int r, int pressed)
{
	ccprintf("Input %s (%d, %d)\n", action[pressed], c, r);
	set_state(c, r, pressed);
	return mkbp_keyboard_add(state);
}

int verify_key(int c, int r, int pressed)
{
	struct host_cmd_handler_args args;
	struct ec_response_get_next_event event;
	int i;

	args.version = 0;
	args.command = EC_CMD_GET_NEXT_EVENT;
	args.params = NULL;
	args.params_size = 0;
	args.response = &event;
	args.response_max = sizeof(event);
	args.response_size = 0;

	if (c >= 0 && r >= 0) {
		ccprintf("Verify %s (%d, %d)\n", action[pressed], c, r);
		set_state(c, r, pressed);

		if (host_command_process(&args) != EC_RES_SUCCESS)
			return 0;

		for (i = 0; i < KEYBOARD_COLS_MAX; ++i)
			if (event.data.key_matrix[i] != state[i])
				return 0;
	} else {
		ccprintf("Verify no events available\n");
		if (host_command_process(&args) != EC_RES_UNAVAILABLE)
			return 0;
	}

	return 1;
}

int verify_key_v2(int c, int r, int pressed, int expect_more)
{
	struct host_cmd_handler_args args;
	struct ec_response_get_next_event_v1 event;
	int i;

	args.version = 2;
	args.command = EC_CMD_GET_NEXT_EVENT;
	args.params = NULL;
	args.params_size = 0;
	args.response = &event;
	args.response_max = sizeof(event);
	args.response_size = 0;

	if (c >= 0 && r >= 0) {
		ccprintf("Verify %s (%d, %d). Expect %smore.\n",
			 action[pressed], c, r, expect_more ? "" : "no ");
		set_state(c, r, pressed);

		if (host_command_process(&args) != EC_RES_SUCCESS)
			return 0;

		if (!!(event.event_type & EC_MKBP_HAS_MORE_EVENTS) !=
		    expect_more) {
			ccprintf("Incorrect more events!\n");
			return 0;
		}

		for (i = 0; i < KEYBOARD_COLS_MAX; ++i)
			if (event.data.key_matrix[i] != state[i])
				return 0;
	} else {
		ccprintf("Verify no events available\n");
		if (host_command_process(&args) != EC_RES_UNAVAILABLE)
			return 0;
	}

	return 1;
}

static int verify_key_v3(int c, int r, int pressed, int expect_more)
{
	struct host_cmd_handler_args args;
	struct ec_response_get_next_event_v3 event;
	int i;

	args.version = 3;
	args.command = EC_CMD_GET_NEXT_EVENT;
	args.params = NULL;
	args.params_size = 0;
	args.response = &event;
	args.response_max = sizeof(event);
	args.response_size = 0;

	if (c >= 0 && r >= 0) {
		ccprintf("Verify %s (%d, %d). Expect %smore.\n",
			 action[pressed], c, r, expect_more ? "" : "no ");
		set_state(c, r, pressed);

		if (host_command_process(&args) != EC_RES_SUCCESS)
			return 0;

		if (!!(event.event_type & EC_MKBP_HAS_MORE_EVENTS) !=
		    expect_more) {
			ccprintf("Incorrect more events!\n");
			return 0;
		}

		for (i = 0; i < KEYBOARD_COLS_MAX; ++i)
			if (event.data.key_matrix[i] != state[i])
				return 0;
	} else {
		ccprintf("Verify no events available\n");
		if (host_command_process(&args) != EC_RES_UNAVAILABLE)
			return 0;
	}

	return 1;
}

int mkbp_config(struct ec_params_mkbp_set_config params)
{
	struct host_cmd_handler_args args;

	args.version = 0;
	args.command = EC_CMD_MKBP_SET_CONFIG;
	args.params = &params;
	args.params_size = sizeof(params);
	args.response = NULL;
	args.response_max = 0;
	args.response_size = 0;

	return host_command_process(&args) == EC_RES_SUCCESS;
}

int set_fifo_size(int sz)
{
	struct ec_params_mkbp_set_config params;

	params.config.valid_mask = EC_MKBP_VALID_FIFO_MAX_DEPTH;
	params.config.valid_flags = 0;
	params.config.fifo_max_depth = sz;

	return mkbp_config(params);
}

int set_kb_scan_enabled(int enabled)
{
	struct ec_params_mkbp_set_config params;

	params.config.valid_mask = 0;
	params.config.valid_flags = EC_MKBP_FLAGS_ENABLE;
	params.config.flags = (enabled ? EC_MKBP_FLAGS_ENABLE : 0);

	return mkbp_config(params);
}

void clear_mkbp_events(void)
{
	struct host_cmd_handler_args args;
	struct ec_response_get_next_event event;

	args.version = 0;
	args.command = EC_CMD_GET_NEXT_EVENT;
	args.params = NULL;
	args.params_size = 0;
	args.response = &event;
	args.response_max = sizeof(event);
	args.response_size = 0;

	/*
	 * We should return EC_RES_UNAVAILABLE if there are no MKBP events left.
	 */
	while (host_command_process(&args) != EC_RES_UNAVAILABLE)
		;
}

/*****************************************************************************/
/* Tests */

int single_key_press(void)
{
	keyboard_clear_buffer();
	clear_state();
	TEST_ASSERT(press_key(0, 0, 1) == EC_SUCCESS);
	TEST_ASSERT(FIFO_NOT_EMPTY());
	TEST_ASSERT(press_key(0, 0, 0) == EC_SUCCESS);
	TEST_ASSERT(FIFO_NOT_EMPTY());

	clear_state();
	TEST_ASSERT(verify_key(0, 0, 1));
	TEST_ASSERT(FIFO_NOT_EMPTY());
	TEST_ASSERT(verify_key(0, 0, 0));
	TEST_ASSERT(FIFO_EMPTY());

	return EC_SUCCESS;
}

int single_key_press_v2(void)
{
	keyboard_clear_buffer();
	clear_state();
	TEST_ASSERT(press_key(0, 0, 1) == EC_SUCCESS);
	TEST_ASSERT(FIFO_NOT_EMPTY());
	TEST_ASSERT(press_key(0, 0, 0) == EC_SUCCESS);
	TEST_ASSERT(FIFO_NOT_EMPTY());

	clear_state();
	TEST_ASSERT(verify_key_v2(0, 0, 1, 1));
	TEST_ASSERT(FIFO_NOT_EMPTY());
	TEST_ASSERT(verify_key_v2(0, 0, 0, 0));
	TEST_ASSERT(FIFO_EMPTY());

	return EC_SUCCESS;
}

int single_key_press_v3(void)
{
	keyboard_clear_buffer();
	clear_state();
	TEST_ASSERT(press_key(0, 0, 1) == EC_SUCCESS);
	TEST_ASSERT(FIFO_NOT_EMPTY());
	TEST_ASSERT(press_key(0, 0, 0) == EC_SUCCESS);
	TEST_ASSERT(FIFO_NOT_EMPTY());

	clear_state();
	TEST_ASSERT(verify_key_v3(0, 0, 1, 1));
	TEST_ASSERT(FIFO_NOT_EMPTY());
	TEST_ASSERT(verify_key_v3(0, 0, 0, 0));
	TEST_ASSERT(FIFO_EMPTY());

	return EC_SUCCESS;
}

int test_fifo_size(void)
{
	keyboard_clear_buffer();
	clear_state();
	TEST_ASSERT(set_fifo_size(1));
	TEST_ASSERT(press_key(0, 0, 1) == EC_SUCCESS);
	TEST_ASSERT(press_key(0, 0, 0) == EC_ERROR_OVERFLOW);

	clear_state();
	TEST_ASSERT(verify_key(0, 0, 1));
	TEST_ASSERT(FIFO_EMPTY());

	/* Restore FIFO size */
	TEST_ASSERT(set_fifo_size(100));

	return EC_SUCCESS;
}

int test_enable(void)
{
	keyboard_clear_buffer();
	clear_state();
	TEST_ASSERT(set_kb_scan_enabled(0));
	TEST_ASSERT(press_key(0, 0, 1) == EC_SUCCESS);
	TEST_ASSERT(FIFO_EMPTY());

	TEST_ASSERT(set_kb_scan_enabled(1));
	TEST_ASSERT(press_key(0, 0, 1) == EC_SUCCESS);
	TEST_ASSERT(FIFO_NOT_EMPTY());
	TEST_ASSERT(verify_key(0, 0, 1));

	return EC_SUCCESS;
}

int fifo_underrun(void)
{
	keyboard_clear_buffer();
	clear_state();
	TEST_ASSERT(press_key(0, 0, 1) == EC_SUCCESS);

	clear_state();
	TEST_ASSERT(verify_key(0, 0, 1));

	/* When FIFO under run, host command reutns last known state */
	TEST_ASSERT(verify_key(-1, -1, -1));

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	ec_int_level = 1;
	test_reset();

	/* Clear any pending events such as lid open. */
	clear_mkbp_events();
	RUN_TEST(single_key_press);
	RUN_TEST(single_key_press_v2);
	RUN_TEST(single_key_press_v3);
	RUN_TEST(test_fifo_size);
	RUN_TEST(test_enable);
	RUN_TEST(fifo_underrun);

	test_print_result();
}
