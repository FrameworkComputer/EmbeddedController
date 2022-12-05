/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @brief Shared mem implementation that uses malloc/free from libc.
 */

#include "common.h"
#include "console.h"
#include "shared_mem.h"
#include "task.h"

#include <stdlib.h>

#include <malloc.h>

int shared_mem_size(void)
{
	struct mallinfo info = mallinfo();

	return info.fordblks + info.fsmblks;
}

int shared_mem_acquire(int size, char **dest_ptr)
{
	*dest_ptr = NULL;

	if (in_interrupt_context())
		return EC_ERROR_INVAL;

	*dest_ptr = malloc(size);
	if (!*dest_ptr)
		return EC_ERROR_MEMORY_ALLOCATION;

	return EC_SUCCESS;
}

void shared_mem_release(void *ptr)
{
	if (in_interrupt_context())
		return;

	free(ptr);
}

#ifdef CONFIG_CMD_SHMEM
static int command_shmem(int argc, const char **argv)
{
	struct mallinfo info = mallinfo();

	ccprintf("Total:         %d\n",
		 info.uordblks + info.fordblks + info.fsmblks);
	ccprintf("Allocated:     %d\n", info.uordblks);
	ccprintf("Free:          %d\n", info.fordblks + info.fsmblks);

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(shmem, command_shmem, NULL,
			     "Print shared memory stats");
#endif /* CONFIG_CMD_SHMEM */
