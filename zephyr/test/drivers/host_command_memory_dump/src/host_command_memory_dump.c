/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "ec_tasks.h"
#include "hooks.h"
#include "host_command.h"
#include "host_command_memory_dump.h"
#include "test/drivers/test_state.h"

#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#define TEST_RETURN_BUFFER_SIZE (256)

void register_thread_memory_dump(k_tid_t thread);

/* Simple local structs for storing a memory dump */
struct mem_dump {
	uint16_t count;
	struct mem_segment *segments;
};
struct mem_segment {
	uint32_t addr;
	uint32_t size;
	uint8_t *mem;
};

/* Utility function for sending a host command.
 * Provides more fine grained control over parameters compared to
 * common host command macros.
 */
static enum ec_status send_host_command(int command, int version,
					const void *params, int params_size,
					void *resp, int resp_max,
					int *resp_size)
{
	int rv;
	struct host_cmd_handler_args args;

	args.version = version;
	args.command = command;
	args.params = params;
	args.params_size = params_size;
	args.response = resp;
	args.response_max = resp_max;
	args.response_size = 0;

	rv = host_command_process(&args);

	if (rv == EC_RES_SUCCESS && resp_size) {
		*resp_size = args.response_size;
	}

	return rv;
}

static void before(void *data)
{
	clear_memory_dump();
}

K_THREAD_STACK_DEFINE(test_stack, 256);

static void test_thread_entry(void *a, void *b, void *c)
{
	while (true) {
		k_msleep(1000);
	}
}

/* Check if a buffer contains a given value */
static bool buffer_contains(const void *buffer, size_t buffer_size,
			    const void *value, size_t value_size)
{
	if (buffer == NULL || value == NULL) {
		return false;
	}

	if (value_size > buffer_size) {
		return false;
	}

	for (size_t i = 0; i <= buffer_size - value_size; i++) {
		if (memcmp((const char *)buffer + i, value, value_size) == 0) {
			return true;
		}
	}
	return false;
}

/* Free a malloc'd memory dump structure */
static void free_mem_dump(struct mem_dump *dump)
{
	for (int i = 0; i < dump->count; i++) {
		free(dump->segments[i].mem);
	}
	free(dump->segments);
}

/*
 * This utility function is similar to memcpy, but uses a mem_dump struct
 * as the source memory. The requested memory may span multiple memory segments.
 * The memory segments are not ordered.
 */
static void *memcpy_from_dump(struct mem_dump *dump, void *dest,
			      const uint32_t src_addr, size_t size)
{
	size_t offset = 0;

	while (offset < size) {
		int seg;
		/* Find the memory segment that contains the source
		 * address + offset.
		 */
		for (seg = 0; seg < dump->count; seg++) {
			if (src_addr + offset >= dump->segments[seg].addr &&
			    src_addr + offset <
				    dump->segments[seg].addr +
					    dump->segments[seg].size) {
				break;
			}
		}
		/* Requested memory not found */
		if (seg >= dump->count) {
			return NULL;
		}
		/* Size of the memory segment, starting from the source address
		 */
		size_t segment_size =
			dump->segments[seg].size -
			(src_addr + offset - dump->segments[seg].addr);
		/* Clamp copy size to min of remaining size and segment_size */
		size_t copy_size = MIN(size - offset, segment_size);

		zassert_not_null(memcpy(((uint8_t *)dest + offset),
					dump->segments[seg].mem + offset,
					copy_size));
		offset += copy_size;
	}

	return dest;
}

/* Util function for fetching a memory dump using host commands. */
static enum ec_status fetch_memory_dump(struct mem_dump *dump)
{
	struct ec_response_memory_dump_get_metadata metadata_response;
	struct ec_response_get_protocol_info protocol_info_response;

	struct ec_response_memory_dump_get_entry_info entry_info_response;
	int seg = 0;

	zassert_ok(send_host_command(EC_CMD_GET_PROTOCOL_INFO, 0, NULL, 0,
				     &protocol_info_response,
				     sizeof(protocol_info_response), NULL));

	zassert_ok(send_host_command(EC_CMD_MEMORY_DUMP_GET_METADATA, 0, NULL,
				     0, &metadata_response,
				     sizeof(metadata_response), NULL));

	dump->count = metadata_response.memory_dump_entry_count;
	dump->segments = (struct mem_segment *)malloc(
		sizeof(struct mem_segment) *
		metadata_response.memory_dump_entry_count);

	for (seg = 0; seg < metadata_response.memory_dump_entry_count; seg++) {
		struct ec_params_memory_dump_get_entry_info entry_info_params = {
			.memory_dump_entry_index = seg
		};

		zassert_ok(send_host_command(
			EC_CMD_MEMORY_DUMP_GET_ENTRY_INFO, 0,
			&entry_info_params, sizeof(entry_info_params),
			&entry_info_response, sizeof(entry_info_response),
			NULL));

		dump->segments[seg].addr = entry_info_response.address;
		dump->segments[seg].size = entry_info_response.size;
		dump->segments[seg].mem =
			(uint8_t *)malloc(entry_info_response.size);

		uint32_t offset = 0;

		while (offset < entry_info_response.size) {
			struct ec_params_memory_dump_read_memory
				read_mem_params = {
					.memory_dump_entry_index = seg,
					.address = entry_info_response.address +
						   offset,
					.size = entry_info_response.size -
						offset,
				};
			uint16_t response_max =
				protocol_info_response.max_response_packet_size;
			uint8_t *read_mem_response = malloc(response_max);
			int response_size = 0;

			zassert_ok(send_host_command(
				EC_CMD_MEMORY_DUMP_READ_MEMORY, 0,
				&read_mem_params, sizeof(read_mem_params),
				read_mem_response, response_max,
				&response_size));

			zassert_true(response_size > 0 &&
				     response_size < response_max);

			zassert_not_null(
				memcpy(dump->segments[seg].mem + offset,
				       read_mem_response, response_size));

			offset += response_size;
		};
	}

	/* try reading out of bounds */
	struct ec_params_memory_dump_read_memory read_mem_params_addr_low = {
		.memory_dump_entry_index = 0,
		.address = 0,
		.size = 4,
	};
	int response_size = 0;
	uint8_t test_response[TEST_RETURN_BUFFER_SIZE];

	zassert_equal(EC_RES_INVALID_PARAM,
		      send_host_command(EC_CMD_MEMORY_DUMP_READ_MEMORY, 0,
					&read_mem_params_addr_low,
					sizeof(read_mem_params_addr_low),
					test_response, TEST_RETURN_BUFFER_SIZE,
					&response_size),
		      NULL);

	zassert_equal(0, response_size, NULL);

	/* try reading illegal size from the last segment */
	struct ec_params_memory_dump_read_memory read_mem_params_bad_size = {
		.memory_dump_entry_index = seg - 1,
		.address = entry_info_response.address,
		.size = entry_info_response.size + 1,
	};

	zassert_equal(EC_RES_INVALID_PARAM,
		      send_host_command(EC_CMD_MEMORY_DUMP_READ_MEMORY, 0,
					&read_mem_params_bad_size,
					sizeof(read_mem_params_bad_size),
					test_response, TEST_RETURN_BUFFER_SIZE,
					&response_size),
		      NULL);

	zassert_equal(0, response_size, NULL);

	/* try creating wraparound in address+size */
	struct ec_params_memory_dump_read_memory read_mem_params_wraparound = {
		.memory_dump_entry_index = seg - 1,
		.address = entry_info_response.address +
			   entry_info_response.size - 1,
		.size = UINT32_MAX,
	};

	zassert_equal(EC_RES_INVALID_PARAM,
		      send_host_command(EC_CMD_MEMORY_DUMP_READ_MEMORY, 0,
					&read_mem_params_wraparound,
					sizeof(read_mem_params_wraparound),
					test_response, TEST_RETURN_BUFFER_SIZE,
					&response_size),
		      NULL);

	zassert_equal(0, response_size, NULL);

	return EC_RES_SUCCESS;
}

/*
 * Ensure that a memory dump returns empty list if requested before being
 * initialized.
 */
ZTEST_USER(memory_dump, test_dump_before_registered)
{
	struct ec_response_memory_dump_get_metadata metadata_response;
	int rv;

	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_MEMORY_DUMP_GET_METADATA, 0, metadata_response);

	rv = host_command_process(&args);

	zassert_equal(metadata_response.memory_dump_entry_count, 0);
}

/* Check if thread stack is included in memory dump */
ZTEST_USER(memory_dump, test_dump_thread_stack)
{
	const uint32_t magic_val_1 = 0x11111111;
	const uint32_t magic_val_2 = 0x22222222;
	const uint32_t magic_val_3 = 0x33333333;
	struct mem_dump dump;
	uint8_t *test_stack_from_dump;
	struct k_thread test_thread_data;

	/* Create a new thread and pass magic values as initial parameters */
	k_tid_t test_thread = k_thread_create(
		&test_thread_data, test_stack,
		K_THREAD_STACK_SIZEOF(test_stack), test_thread_entry,
		(void *)magic_val_1, (void *)magic_val_2, (void *)magic_val_3,
		1, 0, K_NO_WAIT);

	/* Wait for the thread to start */
	k_msleep(100);

	zassert_true(buffer_contains((uint8_t *)test_thread->stack_info.start,
				     test_thread->stack_info.size, &magic_val_1,
				     4));
	zassert_true(buffer_contains((uint8_t *)test_thread->stack_info.start,
				     test_thread->stack_info.size, &magic_val_2,
				     4));
	zassert_true(buffer_contains((uint8_t *)test_thread->stack_info.start,
				     test_thread->stack_info.size, &magic_val_3,
				     4));

	/* Test thread isn't a known thread, so add it here */
	register_thread_memory_dump(test_thread);

	/* Stop test thread */
	k_thread_abort(test_thread);

	/* Fetch memory dump */
	fetch_memory_dump(&dump);

	/* Allocate buffer for thread stack */
	test_stack_from_dump = (uint8_t *)malloc(test_thread->stack_info.size);

	zassert_not_null(test_stack_from_dump);

	/* Copy stack from memory dump */
	zassert_not_null(memcpy_from_dump(&dump, test_stack_from_dump,
					  test_thread->stack_info.start,
					  test_thread->stack_info.size));

	/* Search for magic values in fetched stack memory */
	zassert_true(buffer_contains(test_stack_from_dump,
				     test_thread->stack_info.size, &magic_val_1,
				     4));
	zassert_true(buffer_contains(test_stack_from_dump,
				     test_thread->stack_info.size, &magic_val_2,
				     4));
	zassert_true(buffer_contains(test_stack_from_dump,
				     test_thread->stack_info.size, &magic_val_3,
				     4));

	/* Cleanup */
	free(test_stack_from_dump);
	free_mem_dump(&dump);
}

/* Check if keyscan thread stack is included in memory dump */
ZTEST_USER(memory_dump, test_verify_excluded_threads_not_dumped)
{
	struct mem_dump dump;
	k_tid_t main_thread;
	k_tid_t keyscan_thread;
	k_tid_t keyproto_thread;
	__maybe_unused k_tid_t wov_thread;
	bool main_stack_found;

	main_thread = get_main_thread();
	zassert_not_null(main_thread);
	keyscan_thread = task_id_to_thread_id(TASK_ID_KEYSCAN);
	zassert_not_null(keyscan_thread);
	keyproto_thread = task_id_to_thread_id(TASK_ID_KEYPROTO);
	zassert_not_null(keyproto_thread);
#ifdef HAS_TASK_WOV
	wov_thread = task_id_to_thread_id(TASK_ID_WOV);
	zassert_not_null(wov_thread);
#endif /* HAS_TASK_WOV */

	/* Thread memory should be registered after HOOK_INIT */
	hook_notify(HOOK_INIT);
	/* Fetch memory dump */
	fetch_memory_dump(&dump);

	/*
	 * Verify KEYSCAN and KEYPROTO thread stacks are NOT in dump.
	 * Also verify main thread stack IS in dump (i.e. not empty).
	 */
	main_stack_found = false;
	for (int i = 0; i < dump.count; i++) {
		struct mem_segment seg = dump.segments[i];

		zassert_false(
			seg.addr < keyscan_thread->stack_info.start +
					   keyscan_thread->stack_info.size &&
			seg.addr + seg.size > keyscan_thread->stack_info.start);
		zassert_false(
			seg.addr < keyproto_thread->stack_info.start +
					   keyproto_thread->stack_info.size &&
			seg.addr + seg.size >
				keyproto_thread->stack_info.start);
#ifdef HAS_TASK_WOV
		zassert_false(seg.addr < wov_thread->stack_info.start +
						 wov_thread->stack_info.size &&
			      seg.addr + seg.size >
				      wov_thread->stack_info.start);
#endif /* HAS_TASK_WOV */
		if (seg.addr < main_thread->stack_info.start +
				       main_thread->stack_info.size &&
		    seg.addr + seg.size > main_thread->stack_info.start) {
			main_stack_found = true;
		}
	}
	zassert_true(main_stack_found);

	/* Cleanup */
	free_mem_dump(&dump);
}

ZTEST_SUITE(memory_dump, NULL, NULL, before, NULL, NULL);
