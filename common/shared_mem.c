/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Shared memory module for Chrome EC */

#include "config.h"
#include "link_defs.h"
#include "shared_mem.h"
#include "system.h"

static int buf_in_use;


int shared_mem_size(void)
{
	/* Use all the RAM we can.  The shared memory buffer is the
	 * last thing allocated from the start of RAM, so we can use
	 * everything up to the jump data at the end of RAM. */
	return system_usable_ram_end() - (uint32_t)__shared_mem_buf;
}


int shared_mem_acquire(int size, int wait, char **dest_ptr)
{
	if (size > shared_mem_size() || size <= 0)
		return EC_ERROR_INVAL;

	/* TODO: if task_start() hasn't been called, fail immediately
	 * if not available. */

	/* TODO: wait if requested; for now, we fail immediately if
	 * not available. */
	if (buf_in_use)
		return EC_ERROR_BUSY;

	/* TODO: atomically acquire buf_in_use. */
	buf_in_use = 1;
	*dest_ptr = __shared_mem_buf;
	return EC_SUCCESS;
}


void shared_mem_release(void *ptr)
{
	/* TODO: use event to wake up a previously-blocking acquire */
	buf_in_use = 0;
}
