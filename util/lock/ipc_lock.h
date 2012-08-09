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

#ifndef IPC_LOCK_H__
#define IPC_LOCK_H__

#include <sys/ipc.h>

struct ipc_lock {
	key_t key;         /* provided by the developer */
	int sem;           /* internal */
	int is_held;       /* internal */
};

/* don't use C99 initializers here, so this can be used in C++ code */
#define IPC_LOCK_INIT(key) \
	{ \
		key,       /* name */ \
		-1,        /* sem */ \
		0,         /* is_held */ \
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

#endif /* IPC_LOCK_H__ */
