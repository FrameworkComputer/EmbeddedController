/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Shared memory module for Chrome EC */

#include "common.h"
#include "console.h"
#include "link_defs.h"
#include "shared_mem.h"
#include "system.h"
#include "util.h"

static int buf_in_use;
static int max_used;

int shared_mem_size(void)
{
	/*
	 * Use all the RAM we can.  The shared memory buffer is the last thing
	 * allocated from the start of RAM, so we can use everything up to the
	 * jump data at the end of RAM.
	 */
	return system_usable_ram_end() - (uintptr_t)__shared_mem_buf;
}

int shared_mem_acquire(int size, char **dest_ptr)
{
	if (size > shared_mem_size() || size <= 0)
		return EC_ERROR_INVAL;

	if (buf_in_use)
		return EC_ERROR_BUSY;

	/*
	 * We could guard buf_in_use with a mutex, but since shared memory is
	 * currently only used by debug commands, that's overkill.
	 */

	buf_in_use = size;
	*dest_ptr = __shared_mem_buf;

	if (max_used < size)
		max_used = size;

	return EC_SUCCESS;
}

void shared_mem_release(void *ptr)
{
	buf_in_use = 0;
}

#ifdef CONFIG_CMD_SHMEM
static int command_shmem(int argc, char **argv)
{
	ccprintf("Size:%6d\n", shared_mem_size());
	ccprintf("Used:%6d\n", buf_in_use);
	ccprintf("Max: %6d\n", max_used);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(shmem, command_shmem,
			NULL,
			"Print shared memory stats",
			NULL);
#endif
