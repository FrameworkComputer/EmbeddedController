/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Shared memory module for Chrome EC */

#include "shared_mem.h"
#include "uart.h"

/* Size of shared memory buffer */
#define SHARED_MEM_SIZE 4096

static char shared_buf[SHARED_MEM_SIZE];
static int buf_in_use = 0;


int shared_mem_init(void)
{
	return EC_SUCCESS;
}


int shared_mem_size(void)
{
	return SHARED_MEM_SIZE;
}


int shared_mem_acquire(int size, int wait, char **dest_ptr)
{
	if (size > SHARED_MEM_SIZE || size <= 0)
		return EC_ERROR_INVAL;

	/* TODO: if task_start() hasn't been called, fail immediately
	 * if not available. */

	/* TODO: wait if requested; for now, we fail immediately if
	 * not available. */
	if (buf_in_use)
		return EC_ERROR_BUSY;

	/* TODO: atomically acquire buf_in_use. */
	buf_in_use = 1;
	*dest_ptr = shared_buf;
	return EC_SUCCESS;
}


void shared_mem_release(void *ptr)
{
	/* TODO: use event to wake up a previously-blocking acquire */
	buf_in_use = 0;
}
