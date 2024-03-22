/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic_t.h"
#include "task.h"

enum mutex_r_state {
	MUTEX_R_UNLOCKED = 0,
	MUTEX_R_LOCKED = 1,
	MUTEX_R_LOCKED_WAITING = 2,
};

struct mutex_r {
	atomic_t state;
	atomic_t waiters;
	int count;
	task_id_t owner;
};

/**
 * K_MUTEX_R_DEFINE is a macro that allows to create static mutex without the
 * need to initialize it at runtime.
 */
#define K_MUTEX_R_DEFINE(name)             \
	struct mutex_r name = {            \
		.state = MUTEX_R_UNLOCKED, \
		.waiters = 0,              \
		.count = 0,                \
		.owner = -1,               \
	}

/**
 * Initialize recursive mutex.
 */
void mutex_init_recursive(struct mutex_r *mtx);

/**
 * Lock a recursive mutex.
 *
 * If the mutex is unlocked, lock it and set the 'count' to 1.
 * If the mutex is already locked by the current task, increase the 'count'
 * and let the task go.
 * If the mutex is already locked by another task, de-schedules the current
 * task until the mutex is again unlocked.
 *
 * Must not be used in interrupt context!
 */
void mutex_lock_recursive(struct mutex_r *mtx);

/**
 * Attempt to lock a recursive mutex
 *
 * If the mutex is unlocked, lock it, set the 'count' to 1 and return 1
 * If the mutex is already locked by the current task, increase the 'count'
 * and return 1.
 * If the mutex is already locked by another task, return 0.
 *
 * Must not be used in interrupt context!
 */
int mutex_try_lock_recursive(struct mutex_r *mtx);

/**
 * Decrease the 'count' of recursive mutex.
 *
 * If the 'count' becomes 0, the mutex is unlocked. Must be called on locked
 * mutex only.
 *
 * Must not be used in interrupt context!
 */
void mutex_unlock_recursive(struct mutex_r *mtx);
