/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "panic.h"
#include "panic_log.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(panic_log, LOG_LEVEL_DBG);

/* Forward declare private panic log functions */
uint32_t panic_log_capacity(void);
void panic_log_corrupt(void);
bool panic_log_freeze(bool freeze);
bool panic_log_is_valid(void);
void panic_log_init(void);
uint32_t panic_log_len(void);
char panic_log_read(uint32_t offset);
void panic_log_reset(void);
uint32_t panic_log_version(void);
extern bool panic_log_initialized;
extern bool panic_log_frozen;

/* Reset the panic log before each test. Panic log will begin frozen. */
static void before_panic_log_test(void *state)
{
	zassert_true(panic_log_initialized);
	panic_log_reset();
	zassert_true(panic_log_frozen);
}

ZTEST_SUITE(panic_log, drivers_predicate_post_main, NULL, before_panic_log_test,
	    NULL, NULL);

/* Mock panic_get_data to simulate new panic */
FAKE_VALUE_FUNC(struct panic_data *, panic_get_data);
struct panic_data panic_data_new = {
	.flags = 0,
};
struct panic_data panic_data_old = {
	.flags = PANIC_DATA_FLAG_OLD_HOSTCMD,
};

/*
 * Convenience function for transitioning panic log from unfrozen to frozen.
 * Fails if panic log was not already unfrozen.
 */
static void freeze_panic_log()
{
	zassert_false(panic_log_frozen);
	zassert_false(panic_log_freeze(true));
	zassert_true(panic_log_frozen);
}

/*
 * Convenience function for transitioning panic log from frozen to unfrozen.
 * Fails if panic log was not already frozen.
 */
static void unfreeze_panic_log()
{
	zassert_true(panic_log_frozen);
	zassert_true(panic_log_freeze(false));
	zassert_false(panic_log_frozen);
}

/*
 * Dumps the panic log with a null terminator.
 * Returns the number of bytes read (not including null terminator).
 */
static int dump_panic_log(char *buffer, size_t size)
{
	uint32_t length = panic_log_len();
	/* Need an extra byte for null terminator */
	zassert_true(length <= size - 1);
	for (int i = 0; i < length; i++)
		buffer[i] = panic_log_read(i);
	buffer[length] = '\0';
	return length;
}

/* Returns true if substr is in the panic_log once, and only once. Panic log is
 * expected to be frozen before calling. */
static bool panic_log_contains(const char *substr)
{
	zassert_true(panic_log_frozen);
	zassert_true(panic_log_is_valid());
	char read_buffer[CONFIG_PLATFORM_EC_PANIC_LOG_SIZE + 1];
	int read_count = dump_panic_log(read_buffer, sizeof(read_buffer));
	if (read_count < strlen(substr))
		return false;
	char *found = strstr(read_buffer, substr);
	if (!found)
		return false;
	/* Check if substr appears twice */
	char *found_twice = strstr(found + strlen(substr), substr);
	if (found_twice)
		return false;
	return true;
}

/* Convenience function for writing a string to the panic log. Panic log is
 * expected to already be frozen when called. Verifies the string was written.
 */
static void write_str_to_panic_log(const char *str)
{
	unfreeze_panic_log();
	panic_log_write_str(str, strlen(str));
	freeze_panic_log();
	panic_log_contains(str);
}

/* Simulate panic log being initialized */
static void reinit_panic_log(void)
{
	zassert_true(panic_log_frozen);
	panic_log_initialized = false;
	panic_log_init();
	zassert_true(panic_log_initialized);
}

ZTEST_USER(panic_log, test_write_char)
{
	const char *test_str = __func__;

	unfreeze_panic_log();
	for (int i = 0; i < strlen(test_str); i++)
		panic_log_write_char(test_str[i]);
	freeze_panic_log();
	zassert_true(panic_log_contains(test_str));
}

ZTEST_USER(panic_log, test_write_str)
{
	const char *test_str = __func__;

	unfreeze_panic_log();
	panic_log_write_str(test_str, strlen(test_str));
	freeze_panic_log();
	zassert_true(panic_log_contains(test_str));
}

ZTEST_USER(panic_log, test_printk)
{
	const char *test_str = __func__;

	unfreeze_panic_log();
	printk("%s", test_str);
	freeze_panic_log();
	zassert_true(panic_log_contains(test_str));
}

ZTEST_USER(panic_log, test_log)
{
	const char *test_str = __func__;

	unfreeze_panic_log();
	LOG_INF("%s", test_str);
	freeze_panic_log();
	zassert_true(panic_log_contains(test_str));
}

ZTEST_USER(panic_log, test_cprintf)
{
	const char *test_str = __func__;

	unfreeze_panic_log();
	ccprintf("%s", test_str);
	freeze_panic_log();
	zassert_true(panic_log_contains(test_str));
}

/* Verify panic_printf does not go to panic log */
ZTEST_USER(panic_log, test_panic_printf)
{
	const char *test_str = __func__;

	unfreeze_panic_log();
	panic_printf("%s", test_str);
	freeze_panic_log();
	zassert_false(panic_log_contains(test_str));
}

ZTEST_USER(panic_log, test_reset)
{
	const char *test_str = __func__;

	/* Write test string to panic log */
	write_str_to_panic_log(test_str);
	zassert_true(panic_log_len() > 0);

	/* Reset panic log */
	panic_log_reset();

	/* Verify panic log was reset */
	zassert_true(panic_log_frozen);
	zassert_equal(panic_log_len(), 0);
	zassert_true(panic_log_is_valid());
}

ZTEST_USER(panic_log, test_freeze)
{
	const char *test_str = __func__;

	/* Make sure panic log is not empty */
	unfreeze_panic_log();
	panic_log_write_char('x');
	freeze_panic_log();
	int len_after_freeze = panic_log_len();
	zassert_true(len_after_freeze > 0);

	/* Writing to log should have no affect while frozen */
	panic_log_write_char('x');
	zassert_equal(len_after_freeze, panic_log_len());
	panic_log_write_str(test_str, strlen(test_str));
	zassert_equal(len_after_freeze, panic_log_len());
	zassert_false(panic_log_contains(test_str));

	/* Unfreeze panic log verify it is writable */
	unfreeze_panic_log();
	panic_log_write_str(test_str, strlen(test_str));
	freeze_panic_log();
	zassert_true(panic_log_contains(test_str));
}

/* Verify panic log is discarded during init if it is invalid */
ZTEST_USER(panic_log, test_init_new_panic_invalid)
{
	const char *test_str = __func__;
	panic_get_data_fake.return_val = &panic_data_new;

	/* Write a test string to panic log */
	write_str_to_panic_log(test_str);

	/* Corrupt ring buffer */
	panic_log_corrupt();
	zassert_false(panic_log_is_valid());

	/* Re initialize panic log */
	reinit_panic_log();

	/* Panic log should be reset during init, so string will be missing */
	zassert_false(panic_log_frozen);
	freeze_panic_log();
	zassert_false(panic_log_contains(test_str));
}

/* Verify panic log is frozen during init if it is valid and new panic */
ZTEST_USER(panic_log, test_init_new_panic_valid)
{
	const char *test_str = __func__;
	panic_get_data_fake.return_val = &panic_data_new;

	/* Write a test string to panic log */
	write_str_to_panic_log(test_str);

	/* Re initialize panic log */
	reinit_panic_log();

	/* Panic log should stay frozen after init and test string should still
	 * be present */
	zassert_true(panic_log_frozen);
	zassert_true(panic_log_contains(test_str));
}

/* Verify panic log is restored if panic is old */
ZTEST_USER(panic_log, test_init_old_panic_valid)
{
	const char *test_str = __func__;
	panic_get_data_fake.return_val = &panic_data_old;

	/* Write a test string to panic log */
	write_str_to_panic_log(test_str);

	/* Re initialize panic log */
	reinit_panic_log();

	/* Panic log should be unfrozen and test string should still be present
	 */
	zassert_false(panic_log_frozen);
	freeze_panic_log();
	zassert_true(panic_log_contains(test_str));
}

/* Verify panic log is discarded if panic log is invalid */
ZTEST_USER(panic_log, test_init_old_panic_invalid)
{
	const char *test_str = __func__;
	panic_get_data_fake.return_val = &panic_data_new;

	/* Write a test string to panic log */
	write_str_to_panic_log(test_str);

	/* Corrupt ring buffer */
	panic_log_corrupt();
	zassert_false(panic_log_is_valid());

	/* Re initialize panic log */
	reinit_panic_log();

	/* Panic log should be reset and unfrozen during init,
	 * so test_str should be missing */
	zassert_false(panic_log_frozen);
	freeze_panic_log();
	zassert_false(panic_log_contains(test_str));
}

ZTEST_USER(panic_log, test_host_cmd_info)
{
	const struct ec_params_panic_log_info params = {
		.freeze = 0,
		.unfreeze = 0,
		.reset = 0,
	};
	struct ec_response_panic_log_info response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_PANIC_LOG_INFO, 0, response, params);

	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);
	zassert_equal(response.version, panic_log_version());
	zassert_equal(response.capacity, panic_log_capacity());
	zassert_equal(response.length, panic_log_len());
	zassert_equal(response.valid, panic_log_is_valid());
	zassert_equal(response.frozen, panic_log_frozen);
}

ZTEST_USER(panic_log, test_host_cmd_info_reset)
{
	const char *test_str = __func__;
	const struct ec_params_panic_log_info params = {
		.freeze = 0,
		.unfreeze = 0,
		.reset = 1,
	};
	struct ec_response_panic_log_info response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_PANIC_LOG_INFO, 0, response, params);

	/* Write test string to panic log */
	write_str_to_panic_log(test_str);

	/* Send command to reset */
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);

	/* Verify panic log was reset */
	zassert_false(panic_log_contains(test_str));
}

ZTEST_USER(panic_log, test_host_cmd_info_freeze)
{
	const char *test_str = __func__;
	struct ec_params_panic_log_info params = {
		.freeze = 1,
		.unfreeze = 0,
		.reset = 0,
	};
	struct ec_response_panic_log_info response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_PANIC_LOG_INFO, 0, response, params);

	/* Write test string to panic log */
	write_str_to_panic_log(test_str);

	unfreeze_panic_log();
	zassert_false(panic_log_frozen);
	/* Send command to freeze */
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);
	zassert_true(panic_log_frozen);

	/* Verify panic log was not reset */
	zassert_true(panic_log_contains(test_str));
}

ZTEST_USER(panic_log, test_host_cmd_info_unfreeze)
{
	const char *test_str = __func__;
	struct ec_params_panic_log_info params = {
		.freeze = 0,
		.unfreeze = 1,
		.reset = 0,
	};
	struct ec_response_panic_log_info response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_PANIC_LOG_INFO, 0, response, params);

	/* Write test string to panic log */
	write_str_to_panic_log(test_str);

	zassert_true(panic_log_frozen);
	/* Send command to unfreeze */
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);
	zassert_false(panic_log_frozen);

	/* Verify panic log was not reset */
	freeze_panic_log();
	zassert_true(panic_log_contains(test_str));
}

ZTEST_USER(panic_log, test_host_cmd_info_invalid)
{
	struct ec_params_panic_log_info params = {
		.freeze = 1,
		.unfreeze = 1,
		.reset = 0,
	};
	struct ec_response_panic_log_info response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_PANIC_LOG_INFO, 0, response, params);

	zassert_true(panic_log_frozen);
	/* Send invalid command */
	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM);
}

ZTEST_USER(panic_log, test_host_cmd_read)
{
	struct ec_params_panic_log_read read_params;
	char dump_buffer[CONFIG_PLATFORM_EC_PANIC_LOG_SIZE + 1];
	const int response_max = 250;

	/* Fill panic log */
	unfreeze_panic_log();
	int count = 0;
	while (panic_log_len() < panic_log_capacity()) {
		char write_buffer[16];
		snprintf(write_buffer, sizeof(write_buffer), "%d\n", count++);
		panic_log_write_str(write_buffer, strlen(write_buffer));
	}
	freeze_panic_log();
	zassert_equal(panic_log_len(), panic_log_capacity());

	/* Dump panic log via host command */
	read_params.offset = 0;
	do {
		struct host_cmd_handler_args args = {
			.version = 0,
			.command = EC_CMD_PANIC_LOG_READ,
			.params = &read_params,
			.params_size = sizeof(read_params),
			.response = dump_buffer,
			.response_max = response_max,
			.response_size = 0,
		};
		zassert_ok(host_command_process(&args));
		zassert_true(args.response_size > 0);
		/* If not last packet, reponse_len should be response_max */
		if (read_params.offset + args.response_size < panic_log_len())
			zassert_equal(args.response_size, response_max);
		else
			zassert_true(args.response_size <= response_max);
		read_params.offset += args.response_size;
	} while (read_params.offset < panic_log_len());

	/* Verify panic log dumped via host command matches panic log */
	zassert_true(read_params.offset == panic_log_len());
	dump_buffer[read_params.offset] = '\0';
	zassert_true(panic_log_contains(dump_buffer));
}

ZTEST_USER(panic_log, test_host_cmd_read_invalid)
{
	const char *test_str = __func__;
	struct ec_params_panic_log_read read_params;

	/* Try reading empty panic log */
	read_params.offset = 0;
	struct host_cmd_handler_args args_empty = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_PANIC_LOG_READ, 0, read_params);
	zassert_equal(host_command_process(&args_empty), EC_RES_INVALID_PARAM);

	/* Write something to panic log*/
	write_str_to_panic_log(test_str);

	/* Try reading past the end */
	read_params.offset = panic_log_len();
	struct host_cmd_handler_args args_invalid = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_PANIC_LOG_READ, 0, read_params);
	zassert_equal(host_command_process(&args_invalid),
		      EC_RES_INVALID_PARAM);
}

/* Tests for panic log console commands */

ZTEST_USER(panic_log, test_console_info)
{
	const char *test_str = __func__;
	char expected_buffer[1024];

	/* Write something to panic log*/
	write_str_to_panic_log(test_str);

	snprintf(
		expected_buffer, sizeof(expected_buffer),
		"Valid: %d\r\nFrozen: %d\r\nLength: %d\r\nCapacity: %d\r\nVersion: %d\r\n",
		panic_log_is_valid(), panic_log_frozen, panic_log_len(),
		panic_log_capacity(), panic_log_version());

	CHECK_CONSOLE_CMD("paniclog info", expected_buffer, EC_SUCCESS);
}

ZTEST_USER(panic_log, test_console_reset)
{
	CHECK_CONSOLE_CMD("paniclog reset", "Panic log reset, currently frozen",
			  EC_SUCCESS);
	unfreeze_panic_log();
	CHECK_CONSOLE_CMD("paniclog reset",
			  "Panic log reset, currently unfrozen", EC_SUCCESS);
}

ZTEST_USER(panic_log, test_console_freeze)
{
	CHECK_CONSOLE_CMD("paniclog freeze", "Panic log already frozen",
			  EC_SUCCESS);
	CHECK_CONSOLE_CMD("paniclog unfreeze", "Panic log unfrozen",
			  EC_SUCCESS);
	CHECK_CONSOLE_CMD("paniclog unfreeze", "Panic log already unfrozen",
			  EC_SUCCESS);
	CHECK_CONSOLE_CMD("paniclog freeze", "Panic log frozen", EC_SUCCESS);
}

ZTEST_USER(panic_log, test_console_corrupt)
{
	CHECK_CONSOLE_CMD("paniclog corrupt", "Panic log corrupted",
			  EC_SUCCESS);
}

ZTEST_USER(panic_log, test_console_dump)
{
	/* Can't verify output because dump goes to panic_printf */
	CHECK_CONSOLE_CMD("paniclog dump", "", EC_SUCCESS);
}

ZTEST_SUITE(panic_log_pre_main, drivers_predicate_pre_main, NULL, NULL, NULL,
	    NULL);

/* This should run before panic_log_init runs,*/
ZTEST_USER(panic_log_pre_main, test_pre_init)
{
	panic_get_data_fake.return_val = &panic_data_old;

	/* Verify panic log is uninitialized and frozen */
	zassert_false(panic_log_initialized);
	zassert_true(panic_log_frozen);
	panic_log_init();
	zassert_false(panic_log_frozen);
	zassert_true(panic_log_initialized);
}
