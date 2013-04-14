/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Console commands to trigger flash host commands */

#include "board.h"
#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "system.h"
#include "timer.h"
#include "util.h"

static int error_count;

static int last_write_offset;
static int last_write_size;
static char last_write_data[64];

static int last_erase_offset;
static int last_erase_size;

/*****************************************************************************/
/* Mock functions */
void host_send_response(struct host_cmd_handler_args *args)
{
	/* Do nothing */
}

int flash_write(int offset, int size, const char *data)
{
	last_write_offset = offset;
	last_write_size = size;
	memcpy(last_write_data, data, size);
	return EC_SUCCESS;
}

int flash_erase(int offset, int size)
{
	last_erase_offset = offset;
	last_erase_size = size;
	return EC_SUCCESS;
}

/*****************************************************************************/
/* Test utilities */
#define RUN_TEST(n) \
	do { \
		ccprintf("Running %s...", #n); \
		cflush(); \
		if (n() == EC_SUCCESS) { \
			ccputs("OK\n"); \
		} else { \
			ccputs("Fail\n"); \
			error_count++; \
		} \
	} while (0)

static void begin_verify(void)
{
	last_write_offset = -1;
	last_write_size = -1;
	last_write_data[0] = '\0';
	last_erase_offset = -1;
	last_erase_size = -1;
}

static int verify_write(int offset, int size, const char *data)
{
	int i;

	if (offset != last_write_offset || size != last_write_size)
		return EC_ERROR_UNKNOWN;
	for (i = 0; i < size; ++i)
		if (data[i] != last_write_data[i])
			return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

#define VERIFY_NO_WRITE(off, sz, d) \
	do { \
		begin_verify(); \
		host_command_write(off, sz, d); \
		if (last_write_offset != -1 || last_write_size != -1) \
			return EC_ERROR_UNKNOWN; \
	} while (0)

#define VERIFY_NO_ERASE(off, sz) \
	do { \
		begin_verify(); \
		host_command_erase(off, sz); \
		if (last_erase_offset != -1 || last_erase_size != -1) \
			return EC_ERROR_UNKNOWN; \
	} while (0)

#define VERIFY_WRITE(off, sz, d) \
	do { \
		begin_verify(); \
		host_command_write(off, sz, d); \
		if (verify_write(off, sz, d) != EC_SUCCESS) \
			return EC_ERROR_UNKNOWN; \
	} while (0)

#define VERIFY_ERASE(off, sz) \
	do { \
		begin_verify(); \
		host_command_erase(off, sz); \
		if (last_erase_offset != off || last_erase_size != sz) \
			return EC_ERROR_UNKNOWN; \
	} while (0)

int host_command_write(int offset, int size, const char *data)
{
	struct host_cmd_handler_args args;
	struct ec_params_flash_write params;

	params.offset = offset;
	params.size = size;
	memcpy(params.data, data, size);

	args.version = 0;
	args.command = EC_CMD_FLASH_WRITE;
	args.params = &params;
	args.params_size = sizeof(params);
	args.response = NULL;
	args.response_max = 0;
	args.response_size = 0;

	return host_command_process(&args);
}

int host_command_erase(int offset, int size)
{
	struct host_cmd_handler_args args;
	struct ec_params_flash_write params;

	params.offset = offset;
	params.size = size;

	args.version = 0;
	args.command = EC_CMD_FLASH_ERASE;
	args.params = &params;
	args.params_size = sizeof(params);
	args.response = NULL;
	args.response_max = 0;
	args.response_size = 0;

	return host_command_process(&args);
}

/*****************************************************************************/
/* Tests */
static int test_overwrite_current(void)
{
	uint32_t offset, size;
	const char *d = "TestData0000000"; /* 16 bytes */

	/* Test that we cannot overwrite current image */
	if (system_get_image_copy() == SYSTEM_IMAGE_RO) {
		offset = CONFIG_FW_RO_OFF;
		size = CONFIG_FW_RO_SIZE;
	} else {
		offset = CONFIG_FW_RW_OFF;
		size = CONFIG_FW_RW_SIZE;
	}

	VERIFY_NO_ERASE(offset, sizeof(d));
	VERIFY_NO_ERASE(offset + size - sizeof(d), sizeof(d));
	VERIFY_NO_WRITE(offset, sizeof(d), d);
	VERIFY_NO_WRITE(offset + size - sizeof(d), sizeof(d), d);

	return EC_SUCCESS;
}

static int test_overwrite_other(void)
{
	uint32_t offset, size;
	const char *d = "TestData0000000"; /* 16 bytes */

	/* Test that we can overwrite the other image */
	if (system_get_image_copy() == SYSTEM_IMAGE_RW) {
		offset = CONFIG_FW_RO_OFF;
		size = CONFIG_FW_RO_SIZE;
	} else {
		offset = CONFIG_FW_RW_OFF;
		size = CONFIG_FW_RW_SIZE;
	}

	VERIFY_ERASE(offset, sizeof(d));
	VERIFY_ERASE(offset + size - sizeof(d), sizeof(d));
	VERIFY_WRITE(offset, sizeof(d), d);
	VERIFY_WRITE(offset + size - sizeof(d), sizeof(d), d);

	return EC_SUCCESS;
}

static int command_run_test(int argc, char **argv)
{
	error_count = 0;

	RUN_TEST(test_overwrite_current);
	RUN_TEST(test_overwrite_other);

	if (error_count) {
		ccprintf("Failed %d tests!\n", error_count);
		return EC_ERROR_UNKNOWN;
	} else {
		ccprintf("Pass!\n");
		return EC_SUCCESS;
	}
}
DECLARE_CONSOLE_COMMAND(runtest, command_run_test,
			NULL, NULL, NULL);
