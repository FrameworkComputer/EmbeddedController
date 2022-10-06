/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __UTIL_IPC_LOCK_H
#define __UTIL_IPC_LOCK_H

struct ipc_lock {
	int is_held; /* internal */
	const char *filename; /* provided by the developer */
	int fd; /* internal */
};

/* don't use C99 initializers here, so this can be used in C++ code */
#define LOCKFILE_INIT(lockfile)                  \
	{                                        \
		0, /* is_held */                 \
			lockfile, /* filename */ \
			-1, /* fd */             \
	}

/*
 * acquire_lock: acquire a lock
 *
 * timeout <0 = no timeout (try forever)
 * timeout 0  = do not wait (return immediately)
 * timeout >0 = wait up to $timeout milliseconds (subject to kernel scheduling)
 *
 * return 0   = lock acquired
 * return >0  = lock was already held
 * return <0  = failed to acquire lock
 */
extern int acquire_lock(struct ipc_lock *lock, int timeout_msecs);

/*
 * release_lock: release a lock
 *
 * returns 0 if lock was released successfully
 * returns -1 if lock had not been held before the call
 */
extern int release_lock(struct ipc_lock *lock);

#endif /* __UTIL_IPC_LOCK_H */
