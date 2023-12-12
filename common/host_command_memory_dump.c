/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "host_command_memory_dump.h"
#include "string.h"
#include "task.h"
#include "util.h"

#define MAX_DUMP_ENTRIES 64

struct memory_dump_entry {
	uint32_t address;
	uint32_t size;
};

static struct memory_dump_entry entries[MAX_DUMP_ENTRIES];
static uint16_t memory_dump_entry_count;
K_MUTEX_DEFINE(memory_dump_mutex);

int register_memory_dump(uint32_t address, uint32_t size)
{
	mutex_lock(&memory_dump_mutex);

	if (memory_dump_entry_count >= MAX_DUMP_ENTRIES) {
		mutex_unlock(&memory_dump_mutex);
		ccprintf("ERROR: Memory dump count exceeds max\n");
		return EC_ERROR_OVERFLOW;
	}

	entries[memory_dump_entry_count].address = address;
	entries[memory_dump_entry_count].size = size;

	memory_dump_entry_count += 1;

	mutex_unlock(&memory_dump_mutex);

	return EC_SUCCESS;
}

int clear_memory_dump(void)
{
	mutex_lock(&memory_dump_mutex);
	memset(entries, 0, sizeof(entries));
	memory_dump_entry_count = 0;
	mutex_unlock(&memory_dump_mutex);

	return EC_SUCCESS;
}

static enum ec_status
get_memory_dump_metadata(struct host_cmd_handler_args *args)
{
	struct ec_response_memory_dump_get_metadata *r = args->response;

	mutex_lock(&memory_dump_mutex);

	r->memory_dump_entry_count = memory_dump_entry_count;
	r->memory_dump_total_size = 0;
	for (int i = 0; i < memory_dump_entry_count; i++) {
		r->memory_dump_total_size += entries[i].size;
	}

	mutex_unlock(&memory_dump_mutex);

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_MEMORY_DUMP_GET_METADATA, get_memory_dump_metadata,
		     EC_VER_MASK(0));

static enum ec_status
memory_dump_get_entry_info(struct host_cmd_handler_args *args)
{
	const struct ec_params_memory_dump_get_entry_info *p = args->params;
	struct ec_response_memory_dump_get_entry_info *r = args->response;

	mutex_lock(&memory_dump_mutex);

	if (p->memory_dump_entry_index >= memory_dump_entry_count) {
		mutex_unlock(&memory_dump_mutex);
		return EC_RES_INVALID_PARAM;
	}

	r->address = entries[p->memory_dump_entry_index].address;
	r->size = entries[p->memory_dump_entry_index].size;

	mutex_unlock(&memory_dump_mutex);

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_MEMORY_DUMP_GET_ENTRY_INFO,
		     memory_dump_get_entry_info, EC_VER_MASK(0));

static enum ec_status read_memory_dump(struct host_cmd_handler_args *args)
{
	const struct ec_params_memory_dump_read_memory *p = args->params;
	void *r = args->response;
	struct memory_dump_entry entry;

	mutex_lock(&memory_dump_mutex);

	if (p->memory_dump_entry_index >= memory_dump_entry_count) {
		mutex_unlock(&memory_dump_mutex);
		return EC_RES_INVALID_PARAM;
	}

	entry = entries[p->memory_dump_entry_index];

	if (p->address < entry.address || /* lower bound check */
	    entry.address + entry.size < p->address + p->size || /* upper */
	    p->address + p->size < p->address /* wraparound check */) {
		mutex_unlock(&memory_dump_mutex);
		return EC_RES_INVALID_PARAM;
	}

	/* Must leave room for ec_host_response header */
	args->response_size = MIN(
		p->size, args->response_max - sizeof(struct ec_host_response));

	memcpy(r, (void *)p->address, args->response_size);

	mutex_unlock(&memory_dump_mutex);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_MEMORY_DUMP_READ_MEMORY, read_memory_dump,
		     EC_VER_MASK(0));
