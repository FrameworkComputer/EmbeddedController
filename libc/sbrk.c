/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "link_defs.h"
#include "shared_mem.h"
#include "system.h"

#include <errno.h>

/**
 * Change program's data space by increment bytes.
 *
 * This function is called from the libc sbrk() function (which is in turn
 * called from malloc() when memory needs to be allocated or released).
 *
 * @param incr[in] amount to increment or decrement. 0 means return current
 * program break.
 * @return the previous program break (address) on success
 * @return (void*)-1 on error and errno is set to ENOMEM.
 */
#ifndef CONFIG_ARCH_POSIX
void *_sbrk(intptr_t incr)
#else
void *sbrk(intptr_t incr)
#endif
{
	static char *heap_end = __shared_mem_buf;
	char *prev_heap_end;

	if ((heap_end + incr < __shared_mem_buf) ||
	    (heap_end + incr > (__shared_mem_buf + shared_mem_size()))) {
		errno = ENOMEM;
		return (void *)-1;
	}

	prev_heap_end = heap_end;
	heap_end += incr;

	return (void *)prev_heap_end;
}
