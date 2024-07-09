/* Copyright 2011 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Shared memory interface for Chrome EC.
 *
 * This is intended to supply a relatively large block of memory for use by a
 * task for a relatively short amount of time.  For example, verified boot may
 * need a buffer to hold signature data during a verification operation.  It is
 * NOT intended for allocating long-term buffers; those should in general be
 * static variables allocated at compile-time.  It is NOT a full-featured
 * replacement for malloc() / free().
 */

#ifndef __CROS_EC_SHARED_MEM_H
#define __CROS_EC_SHARED_MEM_H

#include "common.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Returns the maximum amount of shared memory which can be acquired, in
 * bytes.
 */
int shared_mem_size(void);

#define SHARED_MEM_CHECK_SIZE(size) \
	BUILD_ASSERT((size) <= CONFIG_SHAREDMEM_MINIMUM_SIZE)

/**
 * Acquires a shared memory area of the requested size in bytes.
 *
 * Doing a sysjump between images will corrupt and/or erase shared memory as
 * jump tags are added and .bss is reinitialized. Due to the way jump tags are
 * added to the end of RAM, shared memory must not be allocated, accessed, or
 * freed after the start of a sysjump (for example, in HOOK_SYSJUMP).
 *
 * @param size		Number of bytes requested
 * @param dest_ptr	If successful, set on return to the start of the
 *			granted memory buffer.
 *
 * @return EC_SUCCESS if successful, EC_ERROR_BUSY if buffer in use, or
 * other non-zero error code.
 */
int shared_mem_acquire(int size, char **dest_ptr);

#define SHARED_MEM_ACQUIRE_CHECK(size, dest_ptr)        \
	({                                              \
		SHARED_MEM_CHECK_SIZE(size);            \
		shared_mem_acquire((size), (dest_ptr)); \
	})

/**
 * Releases a shared memory area previously allocated via shared_mem_acquire().
 *
 * @param ptr pointer to previously acquired memory
 *
 * @note If ptr is NULL, this is a no-op.
 */
void shared_mem_release(void *ptr);

/*
 * This structure is allocated at the base of the free memory chunk and every
 * allocated buffer.
 */
struct shm_buffer {
	struct shm_buffer *next_buffer;
	struct shm_buffer *prev_buffer;
	size_t buffer_size;
};

#ifdef TEST_SHMALLOC

/*
 * When in test mode, all possible paths in the allocation/free functions set
 * unique bits in an integer bitmap.
 *
 * The test function generates random allocation and free requests and
 * monitors the bitmap until all bits have been set, which indicates that all
 * possible paths have been executed.
 */

#define MAX_MASK_BIT 24
#define ALL_PATHS_MASK ((1 << (MAX_MASK_BIT + 1)) - 1)
void set_map_bit(uint32_t mask);
extern struct shm_buffer *free_buf_chain;
extern struct shm_buffer *allocced_buf_chain;
#endif

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_SHARED_MEM_H */
