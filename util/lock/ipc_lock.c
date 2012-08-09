/* Copyright 2012, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *    * Neither the name of Google Inc. nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <inttypes.h>
#include <stdio.h>
#include <time.h>

#include "csem.h"
#include "ipc_lock.h"

static int lock_init(struct ipc_lock *lock)
{
	if (lock->sem < 0) {
		/* get or create the semaphore, init to 1 if needed */
		int sem = csem_get_or_create(lock->key, 1);
		if (sem < 0) {
			return -1;
		}
		lock->sem = sem;
	}
	return 0;
}

static void msecs_to_timespec(int msecs, struct timespec *tmspec)
{
	tmspec->tv_sec = msecs / 1000;
	tmspec->tv_nsec = (msecs % 1000) * 1000 * 1000;
}

int acquire_lock(struct ipc_lock *lock, int timeout_msecs)
{
	int ret;
	struct timespec timeout;
	struct timespec *timeout_ptr;

	/* initialize the lock */
	if (lock_init(lock) < 0) {
		fprintf(stderr, "%s(): failed to init lock 0x%08x\n",
		        __func__, (uint32_t)lock->key);
		return -1;
	}

	/* check if it is already held */
	if (lock->is_held) {
		return 1;
	}

	/* calculate the timeout */
	if (timeout_msecs >= 0) {
		timeout_ptr = &timeout;
		msecs_to_timespec(timeout_msecs, timeout_ptr);
	} else {
		timeout_ptr = NULL;
	}

	/* try to get the lock */
	ret = csem_down_timeout_undo(lock->sem, timeout_ptr);
	if (ret < 0) {
		fprintf(stderr, "%s(): failed to acquire lock 0x%08x\n",
		        __func__, (uint32_t)lock->key);
		return -1;
	}

	/* success */
	lock->is_held = 1;
	return 0;
}

int release_lock(struct ipc_lock *lock)
{
	if (lock->is_held) {
		lock->is_held = 0;
		csem_up_undo(lock->sem);
		/* NOTE: do not destroy the semaphore, we want it to persist */
		return 0;
	}
        /* did not hold the lock */
        return -1;
}
