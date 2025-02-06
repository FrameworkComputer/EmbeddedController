/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "panic.h"
#include "panic_log.h"
#include "preserved_ring_buf.h"
#include "util.h"

/* Panic log defaults to frozen */
test_export_static bool panic_log_frozen = true;
test_export_static bool panic_log_initialized = false;

/* Update if the schema of the panic log changes */
#define PANIC_LOG_VERSION 1

/* Declare preserved ring buffer */
DECLARE_PRESERVED_RING_BUF(uint8_t, panic_log, CONFIG_PANIC_LOG_SIZE,
			   PANIC_LOG_VERSION);

/* Basic access functions */
test_export_static uint32_t panic_log_len(void)
{
	return preserved_ring_buf_len(panic_log);
}

test_export_static uint32_t panic_log_capacity(void)
{
	return preserved_ring_buf_capacity(panic_log);
}

test_export_static uint32_t panic_log_version(void)
{
	return preserved_ring_buf_version(panic_log);
}

test_export_static bool panic_log_is_valid(void)
{
	return preserved_ring_buf_verify(panic_log);
}

test_export_static char panic_log_read(uint32_t offset)
{
	return preserved_ring_buf_read(panic_log, offset);
}

/* Returns the panic log freeze state before applying the requested freeze state
 */
test_export_static bool panic_log_freeze(bool freeze)
{
	bool orig_frozen = panic_log_frozen;
	panic_log_frozen = freeze;
	return orig_frozen;
}

/* Resets the panic log and defaults to frozen */
test_export_static void panic_log_reset(void)
{
	panic_log_frozen = true;
	preserved_ring_buf_reset(panic_log);
}

/* Check if panic data is new */
static bool panic_data_is_new(void)
{
	struct panic_data *pdata = panic_get_data();
	return !!pdata && !(pdata->flags & PANIC_DATA_FLAG_OLD_HOSTCMD);
}

test_export_static void panic_log_init(void)
{
	bool freeze;
	/* Initialize may only run once */
	if (panic_log_initialized)
		return;
	panic_log_initialized = true;

	if (!panic_log_is_valid()) {
		if (IS_ENABLED(CONFIG_PANIC_LOG_DEBUG))
			ccprintf(
				"Panic log is invalid, resetting and unfreezing panic log\n");
		panic_log_reset();
		freeze = false;
	} else if (panic_data_is_new()) {
		if (IS_ENABLED(CONFIG_PANIC_LOG_DEBUG))
			ccprintf(
				"New panic detected, keeping panic log frozen\n");
		freeze = true;
	} else {
		if (IS_ENABLED(CONFIG_PANIC_LOG_DEBUG))
			ccprintf("No new panic, unfreezing panic log\n");
		freeze = false;
	}
	panic_log_freeze(freeze);
}
DECLARE_HOOK(HOOK_INIT_EARLY, panic_log_init, HOOK_PRIO_DEFAULT - 1);

/* Write a character to the panic log. Character will be dropped if panic log is
 * frozen. */
void panic_log_write_char(const char c)
{
	if (panic_log_frozen)
		return;
	preserved_ring_buf_write(panic_log, c);
}

/* Write a string to the panic log. String will be dropped if panic log is
 * frozen. */
void panic_log_write_str(const char *str, const size_t size)
{
	if (panic_log_frozen)
		return;
	for (int i = 0; i < size; i++)
		preserved_ring_buf_write(panic_log, str[i]);
}

/* Returns the current state of panic log, before applying any requested changes
 */
static enum ec_status
host_command_get_panic_log_info(struct host_cmd_handler_args *args)
{
	struct ec_response_panic_log_info *r = args->response;
	const struct ec_params_panic_log_info *p = args->params;

	if (p->freeze && p->unfreeze)
		return EC_RES_INVALID_PARAM;

	/* Freeze before while reading */
	bool orig_frozen = panic_log_freeze(true);
	r->frozen = orig_frozen;
	r->version = panic_log_version();
	r->capacity = panic_log_capacity();
	r->valid = panic_log_is_valid();
	r->length = panic_log_len();
	args->response_size = sizeof(*r);

	if (p->reset)
		panic_log_reset();

	if (p->freeze)
		panic_log_freeze(true);
	else if (p->unfreeze)
		panic_log_freeze(false);
	else
		/* Restore original frozen state if no change requested */
		panic_log_freeze(orig_frozen);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PANIC_LOG_INFO, host_command_get_panic_log_info,
		     EC_VER_MASK(0));

static enum ec_status
host_command_read_panic_log(struct host_cmd_handler_args *args)
{
	const struct ec_params_panic_log_read *p = args->params;
	char *response = args->response;
	uint32_t offset = p->offset;
	uint32_t length = panic_log_len();

	if (offset >= length)
		return EC_RES_INVALID_PARAM;

	while (offset < length && args->response_size < args->response_max) {
		response[args->response_size++] = panic_log_read(offset++);
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PANIC_LOG_READ, host_command_read_panic_log,
		     EC_VER_MASK(0));

#if defined(CONFIG_PANIC_LOG_DEBUG)

test_export_static void panic_log_corrupt(void)
{
	panic_log->properties->checksum = -1;
}

static int command_panic_log(int argc, const char **argv)
{
	if ((argc == 1) || (argc == 2 && !strcasecmp(argv[1], "info"))) {
		bool orig_frozen = panic_log_freeze(true);
		ccprintf("Valid: %d\n", panic_log_is_valid());
		ccprintf("Frozen: %d\n", orig_frozen);
		ccprintf("Length: %d\n", panic_log_len());
		ccprintf("Capacity: %d\n", panic_log_capacity());
		ccprintf("Version: %d\n", panic_log_version());
		panic_log_freeze(orig_frozen);
	} else if (argc == 2 && !strcasecmp(argv[1], "dump")) {
		bool orig_frozen = panic_log_freeze(true);
		char dump_buffer[16];
		panic_printf("=== Panic Log Start ===\n");
		for (int i = 0; i < panic_log_len();) {
			int j = 0;
			for (; j < ARRAY_SIZE(dump_buffer) - 1 &&
			       i < panic_log_len();
			     j++, i++)
				dump_buffer[j] = panic_log_read(i);
			dump_buffer[j] = '\0';
			/*
			 * Using panic_printf to dump for two reasons:
			 * 1) Avoid dumping panic log back into panic log, since
			 * panic_printf does not go to panic log. 2) Avoid being
			 * re-tokenized if CONFIG_PIGWEED_LOG_TOKENIZED_LIB is
			 * enabled
			 */
			panic_printf("%s", dump_buffer);
		}
		panic_log_freeze(orig_frozen);
		panic_printf("\n=== Panic Log End ===\n");
	} else if (argc == 2 && !strcasecmp(argv[1], "reset")) {
		bool orig_frozen = panic_log_freeze(true);
		panic_log_reset();
		panic_log_freeze(orig_frozen);
		ccprintf("Panic log reset, currently %s\n",
			 orig_frozen ? "frozen" : "unfrozen");
	} else if (argc == 2 && !strcasecmp(argv[1], "freeze")) {
		bool orig_frozen = panic_log_freeze(true);
		if (orig_frozen) {
			ccprintf("Panic log already frozen\n");
		} else {
			ccprintf("Panic log frozen\n");
		}
	} else if (argc == 2 && !strcasecmp(argv[1], "unfreeze")) {
		bool orig_frozen = panic_log_freeze(false);
		if (!orig_frozen) {
			ccprintf("Panic log already unfrozen\n");
		} else {
			ccprintf("Panic log unfrozen\n");
		}
	} else if (argc == 2 && !strcasecmp(argv[1], "corrupt")) {
		panic_log_corrupt();
		ccprintf("Panic log corrupted\n");
	} else {
		return EC_ERROR_PARAM1;
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(paniclog, command_panic_log,
			"[info | dump | reset | freeze | unfreeze | corrupt]",
			"Panic log");

#endif /* CONFIG_PANIC_LOG_DEBUG */
