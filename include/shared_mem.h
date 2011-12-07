/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Shared memory interface for Chrome EC.
 *
 * This is intended to supply a relatively large block of memory for
 * use by a task for a relatively short amount of time.  For example,
 * verified boot may need a buffer to hold signature data during a
 * verification operation.  It is NOT intended for allocating
 * long-term buffers; those should in general be static variables
 * allocated at compile-time.  It is NOT a full-featured replacement
 * for malloc() / free(). */

#ifndef __CROS_EC_SHARED_MEM_H
#define __CROS_EC_SHARED_MEM_H

#include "common.h"

/* Initializes the module. */
int shared_mem_init(void);

/* Returns the maximum amount of shared memory which can be acquired,
 * in bytes. */
int shared_mem_size(void);

/* Acquires a shared memory area of the requested size in bytes.  If
 * wait != 0, will wait for the area to be available; if wait == 0,
 * will fail with EC_ERROR_BUSY if the request cannot be fulfilled
 * immediately.  On success, sets *dest_ptr to the start of the memory
 * area and returns EC_SUCCESS. */
int shared_mem_acquire(int size, int wait, char **dest_ptr);

/* Releases a shared memory area previously allocated via
 * shared_mem_acquire(). */
void shared_mem_release(void *ptr);

#endif  /* __CROS_EC_SHARED_MEM_H */
