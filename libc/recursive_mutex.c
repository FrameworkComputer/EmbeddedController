/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "builtin/assert.h"
#include "recursive_mutex.h"
#include "system.h"
#include "task.h"

/* Recursive mutex implementation */

/* Make sure atomic_t type is enough to keep waiters bitmask. */
BUILD_ASSERT(sizeof(atomic_t) * 8 >= TASK_ID_COUNT);

void mutex_init_recursive(struct mutex_r *mtx)
{
	ASSERT(mtx != NULL);

	mtx->state = MUTEX_R_UNLOCKED;
	mtx->waiters = 0;
	mtx->count = 0;
	mtx->owner = -1;
}

void mutex_lock_recursive(struct mutex_r *mtx)
{
	atomic_val_t state;
	task_id_t current;

	ASSERT(mtx != NULL);
	ASSERT(!in_interrupt_context());

	current = task_get_current();

	/*
	 * Attempt to lock the mutex or check if we are not already owning it.
	 */
	state = MUTEX_R_UNLOCKED;
	if (!atomic_compare_exchange(&mtx->state, &state, MUTEX_R_LOCKED) &&
	    mtx->owner != current) {
		/*
		 * Mutex is already locked and we are not the owner.
		 * Add oneself to waiters.
		 */
		atomic_or(&mtx->waiters, 1U << current);

		/*
		 * Set the mutex state to MUTEX_R_LOCKED_WAITING.
		 *
		 * Escape while loop only when previous mutex state was
		 * MUTEX_R_UNLOCKED. Exchange operation will lock the
		 * mutex atomically (so we will leave while loop as a new
		 * owner).
		 *
		 * NOTE: We must set the state to MUTEX_R_LOCKED_WAITING here
		 * because there may be other tasks waiting.
		 */
		while (atomic_exchange(&mtx->state, MUTEX_R_LOCKED_WAITING) !=
		       MUTEX_R_UNLOCKED) {
			/* Failed to lock the mutex, let's wait for event. */
			task_wait_event_mask(TASK_EVENT_MUTEX, 0);
		}

		/* We are not waiting for this mutex, so remove from waiters. */
		atomic_clear_bits(&mtx->waiters, 1U << current);
	}

	/* We are a mutex owner at this point, so update owner. */
	mtx->owner = current;
	mtx->count++;
}

int mutex_try_lock_recursive(struct mutex_r *mtx)
{
	atomic_val_t state;
	task_id_t current;

	ASSERT(mtx != NULL);
	ASSERT(!in_interrupt_context());

	current = task_get_current();

	/* Attempt to lock the mutex or check if we are not already owning it.
	 */
	state = MUTEX_R_UNLOCKED;
	if (!atomic_compare_exchange(&mtx->state, &state, MUTEX_R_LOCKED) &&
	    mtx->owner != current) {
		/* Mutex is already locked and we are not the owner. */
		return 0;
	}

	/* Only owner can escape if above. */

	mtx->owner = current;
	mtx->count++;

	return 1;
}

void mutex_unlock_recursive(struct mutex_r *mtx)
{
	uint32_t waiters;

	ASSERT(mtx != NULL);
	ASSERT(!in_interrupt_context());

	/* Panic if mutex is not locked. */
	ASSERT(atomic_load(&mtx->state) != MUTEX_R_UNLOCKED);

	/* Panic if not called by the owner */
	ASSERT(mtx->owner == task_get_current());
	ASSERT(mtx->count > 0);

	mtx->count--;
	if (mtx->count == 0) {
		/*
		 * Invalidate mutex owner before releasing mutex so that current
		 * task doesn't overwrite the new owner.
		 */
		mtx->owner = -1;

		/*
		 * Atomically change the mutex state to MUTEX_R_UNLOCKED and
		 * check if the previous state was MUTEX_R_LOCKED_WAITING.
		 */
		if (atomic_exchange(&mtx->state, MUTEX_R_UNLOCKED) ==
		    MUTEX_R_LOCKED_WAITING) {
			/*
			 * Ping waiting tasks
			 *
			 * It's possible that there will be no waiting tasks
			 * (because waiting bit could be cleared by task that
			 * got the mutex after unlocking it).
			 */
			waiters = atomic_load(&mtx->waiters);
			if (waiters) {
				task_set_event(__fls(waiters),
					       TASK_EVENT_MUTEX);
			}
		}
	}
}
