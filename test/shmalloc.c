/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "link_defs.h"
#include "shared_mem.h"
#include "test_util.h"

/*
 * Total size of memory in the malloc pool (shared between free and allocated
 * buffers.
 */
static int total_size;

/*
 * Number of randomized allocation/free attempts, large enough to execute all
 * branches in the malloc/free module.
 */
static int counter = 500000;

/*
 * A good random number generator approximation. Guaranteed to generate the
 * same sequence on all test runs.
 */
static uint32_t next = 127;
static uint32_t myrand(void)
{
	next = next * 1103515245 + 12345;
	return ((uint32_t)(next/65536) % 32768);
}

/* Keep track of buffers allocated by the test function. */
static struct {
	void *buf;
	size_t buffer_size;
} allocations[12];  /* Up to 12 buffers could be allocated concurrently. */

/*
 * Verify that allocated and free buffers do not overlap, and that our and
 * malloc's ideas of the number of allocated buffers match.
 */

static int check_for_overlaps(void)
{
	int i;
	int allocation_match;
	int allocations_count, allocated_count;

	allocations_count = allocated_count = 0;
	for (i = 0; i < ARRAY_SIZE(allocations); i++) {
		struct shm_buffer *allocced_buf;

		if (!allocations[i].buf)
			continue;

		/*
		 * Indication of finding the allocated buffer in internal
		 * malloc structures.
		 */
		allocation_match = 0;

		/* number of buffers allocated by the test program. */
		allocations_count++;

		/*
		 * Number of allocated buffers malloc knows about, calculated
		 * multiple times to keep things simple.
		 */
		allocated_count = 0;
		for (allocced_buf = allocced_buf_chain;
		     allocced_buf;
		     allocced_buf = allocced_buf->next_buffer) {
			int allocated_size, allocation_size;

			allocated_count++;
			if (allocations[i].buf != (allocced_buf + 1))
				continue;

			allocated_size = allocced_buf->buffer_size;
			allocation_size = allocations[i].buffer_size;

			/*
			 * Verify that size requested by the allocator matches
			 * the value used by malloc, i.e. does not exceed the
			 * allocated size and is no less than two buffer
			 * structures lower (which can happen when the
			 * requested size was rounded up to cover gaps smaller
			 * than the buffer header structure size).
			 */
			if ((allocation_size > allocated_size) ||
			    ((allocated_size - allocation_size) >=
			     (2 * sizeof(struct shm_buffer) + sizeof(int)))) {
				ccprintf("inconsistency: allocated (size %d)"
					 " allocation %d(size %d)\n",
					 allocated_size, i, allocation_size);
				return 0;
			}

			if (allocation_match++) {
				ccprintf("inconsistency: duplicated match\n");
				return 0;
			}
		}
		if (!allocation_match) {
			ccprintf("missing match %pP!\n", allocations[i].buf);
			return 0;
		}
	}
	if (allocations_count != allocated_count) {
		ccprintf("count mismatch (%d != %d)!\n",
			 allocations_count, allocated_count);
		return 0;
	}
	return 1;
}

/*
 * Verify that shared memory is in a consistent state, i.e. that there is no
 * overlaps between allocated and free buffers, and that all memory is
 * accounted for (is either allocated or available).
 */

static int shmem_is_ok(int line)
{
	int count = 0;
	int running_size = 0;
	struct shm_buffer *pbuf = free_buf_chain;

	if (pbuf && pbuf->prev_buffer) {
		ccprintf("Bad free buffer list start %pP\n", pbuf);
		goto bailout;
	}

	while (pbuf) {
		struct shm_buffer *top;

		running_size += pbuf->buffer_size;
		if (count++ > 100)
			goto bailout;  /* Is there a loop? */

		top = (struct shm_buffer *)((uintptr_t)pbuf +
					     pbuf->buffer_size);
		if (pbuf->next_buffer) {
			if (top >= pbuf->next_buffer) {
				ccprintf("%s:%d"
					 " - inconsistent buffer size at %pP\n",
					 __func__, __LINE__, pbuf);
				goto bailout;
			}
			if (pbuf->next_buffer->prev_buffer != pbuf) {
				ccprintf("%s:%d"
					 " - inconsistent next buffer at %pP\n",
					 __func__, __LINE__, pbuf);
				goto bailout;
			}
		}
		pbuf = pbuf->next_buffer;
	}

	if (pbuf) { /* Must be a loop. */
		ccprintf("Too many buffers in the chain\n");
		goto bailout;
	}

	/* Make sure there were at least 5 buffers allocated at one point. */
	if (count > 5)
		set_map_bit(1 << 24);

	/* Add allocated sizes. */
	for (pbuf = allocced_buf_chain; pbuf; pbuf = pbuf->next_buffer)
		running_size += pbuf->buffer_size;

	if (total_size) {
		if (total_size != running_size)
			goto bailout;
	} else {
		/* Remember total size for future reference. */
		total_size = running_size;
	}

	if (!check_for_overlaps())
		goto bailout;

	return 1;

 bailout:
	ccprintf("Line %d, counter %d. The list has been corrupted, "
		 "total size %d, running size %d\n",
		 line, counter, total_size, running_size);
	return 0;
}

/*
 * Bitmap used to keep track of branches taken by malloc/free routines. Once
 * all bits in the 0..(MAX_MASK_BIT - 1) range are set, consider the test
 * completed.
 */
static uint32_t test_map;

void run_test(int argc, char **argv)
{
	int index;
	const int shmem_size = shared_mem_size();

	while (counter--) {
		char *shptr;
		uint32_t r_data;

		r_data = myrand();

		if (!(counter % 50000))
			ccprintf("%d\n", counter);

		/*
		 * If all bits we care about are set in the map - the test is
		 * over.
		 */
		if ((test_map & ALL_PATHS_MASK) == ALL_PATHS_MASK) {
			if (test_map & ~ALL_PATHS_MASK) {
				ccprintf("Unexpected mask bits set: %x"
					 ", counter %d\n",
					 test_map & ~ALL_PATHS_MASK,
					 counter);
				test_fail();
				return;
			}
			ccprintf("Done testing, counter at %d\n", counter);
			test_pass();
			return;
		}

		/* Pick a random allocation entry. */
		index = r_data % ARRAY_SIZE(allocations);
		if (allocations[index].buf) {
			/*
			 * If there is a buffer associated with the entry -
			 * release it.
			 */
			shared_mem_release(allocations[index].buf);
			allocations[index].buf = 0;
			if (!shmem_is_ok(__LINE__)) {
				test_fail();
				return;
			}
		} else {
			size_t alloc_size = r_data % (shmem_size);

			/*
			 * If the allocation entry is empty - allocate a
			 * buffer of a random size up to max shared memory.
			 */
			if (shared_mem_acquire(alloc_size, &shptr) ==
			    EC_SUCCESS) {
				allocations[index].buf = (void *) shptr;
				allocations[index].buffer_size = alloc_size;

				/*
				 * Make sure every allocated byte is
				 * modified.
				 */
				while (alloc_size--)
					shptr[alloc_size] =
					shptr[alloc_size] ^ 0xff;

				if (!shmem_is_ok(__LINE__)) {
					test_fail();
					return;
				}
			}
		}
	}

	/*
	 * The test is over, free all still allcated buffers, if any. Keep
	 * verifying memory consistency after each free() invocation.
	 */
	for (index = 0; index < ARRAY_SIZE(allocations); index++)
		if (allocations[index].buf) {
			shared_mem_release(allocations[index].buf);
			allocations[index].buf = NULL;
			if (!shmem_is_ok(__LINE__)) {
				test_fail();
				return;
			}
		}

	ccprintf("Did not pass all paths, map %x != %x\n",
		 test_map, ALL_PATHS_MASK);
	test_fail();
}

void set_map_bit(uint32_t mask)
{
	test_map |= mask;
}
