/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Malloc/free memory module for Chrome EC */
#include <stdint.h>

#include "common.h"
#include "hooks.h"
#include "link_defs.h"
#include "shared_mem.h"
#include "system.h"
#include "task.h"
#include "util.h"

static struct mutex shmem_lock;

#ifndef TEST_SHMALLOC
#define set_map_bit(x)
#define TEST_GLOBAL static
#else
#define TEST_GLOBAL
#endif

/*
 * At the beginning there is a single free memory chunk which includes all
 * memory available in the system. It then gets fragmented/defragmented based
 * on actual allocations/releases.
 */
TEST_GLOBAL struct shm_buffer *free_buf_chain;

/* At the beginning there is no allocated buffers */
TEST_GLOBAL struct shm_buffer *allocced_buf_chain;

/* The size of the biggest ever allocated buffer. */
static int max_allocated_size;

static void shared_mem_init(void)
{
	/*
	 * Use all the RAM we can. The shared memory buffer is the last thing
	 * allocated from the start of RAM, so we can use everything up to the
	 * jump data at the end of RAM.
	 */
	free_buf_chain = (struct shm_buffer *)__shared_mem_buf;
	free_buf_chain->next_buffer = NULL;
	free_buf_chain->prev_buffer = NULL;
	free_buf_chain->buffer_size = system_usable_ram_end() -
		(uintptr_t)__shared_mem_buf;
}
DECLARE_HOOK(HOOK_INIT, shared_mem_init, HOOK_PRIO_FIRST);

/* Called with the mutex lock acquired. */
static void do_release(struct shm_buffer *ptr)
{
	struct shm_buffer *pfb;
	struct shm_buffer *top;
	size_t released_size;

	/* Take the buffer out of the allocated buffers chain. */
	if (ptr == allocced_buf_chain) {
		if (ptr->next_buffer) {
			set_map_bit(BIT(20));
			ptr->next_buffer->prev_buffer = NULL;
		} else {
			set_map_bit(BIT(21));
		}
		allocced_buf_chain = ptr->next_buffer;
	} else {
		/*
		 * Saninty check: verify that the buffer is in the allocated
		 * buffers chain.
		 */
		for (pfb = allocced_buf_chain->next_buffer;
		     pfb;
		     pfb = pfb->next_buffer)
			if (pfb == ptr)
				break;
		if (!pfb)
			return;

		ptr->prev_buffer->next_buffer = ptr->next_buffer;
		if (ptr->next_buffer) {
			set_map_bit(BIT(22));
			ptr->next_buffer->prev_buffer = ptr->prev_buffer;
		} else {
			set_map_bit(BIT(23));
		}
	}

	/*
	 * Let's bring the released buffer back into the fold. Cache its size
	 * for quick reference.
	 */
	released_size = ptr->buffer_size;
	if (!free_buf_chain) {
		/*
		 * All memory had been allocated - this buffer is going to be
		 * the only available free space.
		 */
		set_map_bit(BIT(0));
		free_buf_chain = ptr;
		free_buf_chain->buffer_size = released_size;
		free_buf_chain->next_buffer = NULL;
		free_buf_chain->prev_buffer = NULL;
		return;
	}

	if (ptr < free_buf_chain) {
		/*
		 * Insert this buffer in the beginning of the chain, possibly
		 * merging it with the first buffer of the chain.
		 */
		pfb = (struct shm_buffer *)((uintptr_t)ptr + released_size);
		if (pfb == free_buf_chain) {
			set_map_bit(BIT(1));
			/* Merge the two buffers. */
			ptr->buffer_size = free_buf_chain->buffer_size +
				released_size;
			ptr->next_buffer =
				free_buf_chain->next_buffer;
		} else {
			set_map_bit(BIT(2));
			ptr->buffer_size = released_size;
			ptr->next_buffer = free_buf_chain;
			free_buf_chain->prev_buffer = ptr;
		}
		if (ptr->next_buffer) {
			set_map_bit(BIT(3));
			ptr->next_buffer->prev_buffer = ptr;
		} else {
			set_map_bit(BIT(4));
		}
		ptr->prev_buffer = NULL;
		free_buf_chain = ptr;
		return;
	}

	/*
	 * Need to merge the new free buffer into the existing chain. Find a
	 * spot for it, it should be above the highest address buffer which is
	 * still below the new one.
	 */
	pfb = free_buf_chain;
	while (pfb->next_buffer && (pfb->next_buffer < ptr))
		pfb = pfb->next_buffer;

	top = (struct shm_buffer *)((uintptr_t)pfb + pfb->buffer_size);
	if (top == ptr) {
		/*
		 * The returned buffer is adjacent to an existing free buffer,
		 * below it, merge the two buffers.
		 */
		pfb->buffer_size += released_size;

		/*
		 * Is the returned buffer the exact gap between two free
		 * buffers?
		 */
		top = (struct shm_buffer *)((uintptr_t)ptr + released_size);
		if (top == pfb->next_buffer) {
			/* Yes, it is. */
			pfb->buffer_size += pfb->next_buffer->buffer_size;
			pfb->next_buffer =
				pfb->next_buffer->next_buffer;
			if (pfb->next_buffer) {
				set_map_bit(BIT(5));
				pfb->next_buffer->prev_buffer = pfb;
			} else {
				set_map_bit(BIT(6));
			}
		}
		return;
	}

	top = (struct shm_buffer *)((uintptr_t)ptr + released_size);
	if (top == pfb->next_buffer) {
		/* The new buffer is adjacent with the one right above it. */
		set_map_bit(BIT(7));
		ptr->buffer_size = released_size +
			pfb->next_buffer->buffer_size;
		ptr->next_buffer = pfb->next_buffer->next_buffer;
	} else {
		/* Just include the new free buffer into the chain. */
		set_map_bit(BIT(8));
		ptr->next_buffer = pfb->next_buffer;
		ptr->buffer_size = released_size;
	}
	ptr->prev_buffer = pfb;
	pfb->next_buffer = ptr;
	if (ptr->next_buffer) {
		set_map_bit(BIT(9));
		ptr->next_buffer->prev_buffer = ptr;
	} else {
		set_map_bit(BIT(10));
	}
}

/* Called with the mutex lock acquired. */
static int do_acquire(int size, struct shm_buffer **dest_ptr)
{
	int headroom = 0x10000000; /* we'll never have this much. */
	struct shm_buffer *pfb;
	struct shm_buffer *candidate = 0;

	/* To keep things simple let's align the size. */
	size = (size + sizeof(int) - 1) & ~(sizeof(int) - 1);

	/* And let's allocate room to fit the buffer header. */
	size += sizeof(struct shm_buffer);

	pfb = free_buf_chain;
	while (pfb) {
		if ((pfb->buffer_size >= size) &&
		    ((pfb->buffer_size - size) < headroom)) {
			/* this is a new candidate. */
			headroom = pfb->buffer_size - size;
			candidate = pfb;
		}
		pfb = pfb->next_buffer;
	}

	if (!candidate) {
		set_map_bit(BIT(11));
		return EC_ERROR_BUSY;
	}

	*dest_ptr = candidate;

	/* Now let's take the candidate out of the free buffer chain. */
	if (headroom <= sizeof(struct shm_buffer)) {
		/*
		 * The entire buffer should be allocated, there is no need to
		 * re-define its tail as a new free buffer.
		 */
		if (candidate == free_buf_chain) {
			/*
			 * The next buffer becomes the head of the free buffer
			 * chain.
			 */
			free_buf_chain = candidate->next_buffer;
			if (free_buf_chain) {
				set_map_bit(BIT(12));
				free_buf_chain->prev_buffer = 0;
			} else {
				set_map_bit(BIT(13));
			}
		} else {
			candidate->prev_buffer->next_buffer =
				candidate->next_buffer;
			if (candidate->next_buffer) {
				set_map_bit(BIT(14));
				candidate->next_buffer->prev_buffer =
					candidate->prev_buffer;
			} else {
				set_map_bit(BIT(15));
			}
		}
		return EC_SUCCESS;
	}

	candidate->buffer_size = size;

	/* Candidate's tail becomes a new free buffer. */
	pfb = (struct shm_buffer *)((uintptr_t)candidate + size);
	pfb->buffer_size = headroom;
	pfb->next_buffer = candidate->next_buffer;
	pfb->prev_buffer = candidate->prev_buffer;

	if (pfb->next_buffer) {
		set_map_bit(BIT(16));
		pfb->next_buffer->prev_buffer = pfb;
	} else {
		set_map_bit(BIT(17));
	}

	if (candidate == free_buf_chain) {
		set_map_bit(BIT(18));
		free_buf_chain = pfb;
	} else {
		set_map_bit(BIT(19));
		pfb->prev_buffer->next_buffer = pfb;
	}
	return EC_SUCCESS;
}

int shared_mem_size(void)
{
	struct shm_buffer *pfb;
	size_t max_available = 0;

	mutex_lock(&shmem_lock);

	/* Find the maximum available buffer size. */
	pfb = free_buf_chain;
	while (pfb) {
		if (pfb->buffer_size > max_available)
			max_available = pfb->buffer_size;
		pfb = pfb->next_buffer;
	}

	mutex_unlock(&shmem_lock);
	/* Leave room for shmem header */
	max_available -= sizeof(struct shm_buffer);
	return max_available;
}

int shared_mem_acquire(int size, char **dest_ptr)
{
	int rv;
	struct shm_buffer *new_buf;

	*dest_ptr = NULL;

	if (in_interrupt_context())
		return EC_ERROR_INVAL;

	if (!free_buf_chain)
		return EC_ERROR_BUSY;

	mutex_lock(&shmem_lock);
	rv = do_acquire(size, &new_buf);
	if (rv == EC_SUCCESS) {
		new_buf->next_buffer = allocced_buf_chain;
		new_buf->prev_buffer = NULL;
		if (allocced_buf_chain)
			allocced_buf_chain->prev_buffer = new_buf;

		allocced_buf_chain = new_buf;

		*dest_ptr = (void *)(new_buf + 1);

		if (size > max_allocated_size)
			max_allocated_size = size;
	}
	mutex_unlock(&shmem_lock);

	return rv;
}

void shared_mem_release(void *ptr)
{
	if (in_interrupt_context())
		return;

	mutex_lock(&shmem_lock);
	do_release((struct shm_buffer *)ptr - 1);
	mutex_unlock(&shmem_lock);
}

#ifdef CONFIG_CMD_SHMEM

static int command_shmem(int argc, char **argv)
{
	size_t allocated_size;
	size_t free_size;
	size_t max_free;
	struct shm_buffer *buf;

	allocated_size = free_size = max_free = 0;

	mutex_lock(&shmem_lock);

	for (buf = free_buf_chain; buf; buf = buf->next_buffer) {
		size_t buf_room;

		buf_room = buf->buffer_size;

		free_size += buf_room;
		if (buf_room > max_free)
			max_free = buf_room;
	}

	for (buf = allocced_buf_chain; buf;
	     buf = buf->next_buffer)
		allocated_size += buf->buffer_size;

	mutex_unlock(&shmem_lock);

	ccprintf("Total:         %6zd\n", allocated_size + free_size);
	ccprintf("Allocated:     %6zd\n", allocated_size);
	ccprintf("Free:          %6zd\n", free_size);
	ccprintf("Max free buf:  %6zd\n", max_free);
	ccprintf("Max allocated: %6d\n", max_allocated_size);
	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(shmem, command_shmem,
			     NULL,
			     "Print shared memory stats");

#endif  /* CONFIG_CMD_SHMEM  ^^^^^^^ defined */
