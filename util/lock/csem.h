/*
 * Copyright 2003 Sun Microsystems, Inc.
 * Copyright 2010 Google, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of Google Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
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
 *
 * Developer's note: This was open sourced by Sun Microsystems, which got it
 * via Cobalt Networks.  It has been fairly extensively modified since then.
 */

#ifndef CSEM_H__
#define CSEM_H__

#include <sys/ipc.h>
#include <time.h>

/* create a private key */
#define CSEM_PRIVATE IPC_PRIVATE

/*
 * Create a new semaphore with the specified key, initialized to the
 * specified value.  If the key is CSEM_PRIVATE, a new private semaphore
 * is allocated.
 *
 * Returns the sempahore ID (>= 0) on success.
 * Returns < 0 on error, or if the key already exists.
 */
extern int csem_create(key_t key, unsigned val);

/*
 * Fetch an existing semaphore with the specified key.
 *
 * Returns the sempahore ID (>= 0) on success.
 * Returns < 0 on error, or if the key does not exist.
 */
extern int csem_get(key_t key);

/*
 * Fetch or create a semaphore with the specified key.  If the semaphore
 * did not exist, it will be created with the specified value.
 *
 * Returns the sempahore ID (>= 0) on success.
 * Returns < 0 on error.
 */
extern int csem_get_or_create(key_t key, unsigned val);

/*
 * Destroy the semaphore.
 *
 * Returns 0 on success.
 * Returns < 0 on error.
 */
extern int csem_destroy(int sem_id);

/*
 * Get the value of the semaphore.
 *
 * Returns the value (>= 0) on success.
 * Returns < 0 on error.
 */
extern int csem_getval(int sem_id);

/*
 * Set the value of the semaphore.
 *
 * Returns 0 on success.
 * Returns < 0 on error.
 */
extern int csem_setval(int sem_id, unsigned val);

/*
 * Increment the semaphore.
 *
 * Returns 0 on success.
 * Returns < 0 on error.
 */
extern int csem_up(int sem_id);

/*
 * Increment the semaphore.  This operation will be undone when the
 * process terminates.
 *
 * Returns 0 on success.
 * Returns < 0 on error.
 */
extern int csem_up_undo(int sem_id);

/*
 * Decrement the semaphore, or block if sem == 0.
 *
 * Returns 0 on success.
 * Returns < 0 on error.
 */
extern int csem_down(int sem_id);

/*
 * Decrement the semaphore, or block if sem == 0.  This operation will be
 * undone when the process terminates.
 *
 * Returns 0 on success.
 * Returns < 0 on error.
 */
extern int csem_down_undo(int sem_id);

/*
 * Decrement the semaphore, or block with a timeout if sem == 0.
 *
 * Returns 0 on success.
 * Returns < 0 on error.
 */
extern int csem_down_timeout(int sem_id, struct timespec *timeout);

/*
 * Decrement the semaphore, or block with a timeout if sem == 0.  This
 * operation will be undone when the process terminates.
 *
 * Returns 0 on success.
 * Returns < 0 on error.
 */
extern int csem_down_timeout_undo(int sem_id, struct timespec *timeout);

/*
 * Get the timestamp of the last csem_up()/csem_down() call.
 *
 * Returns sem_otime on success.
 * Returns < 0 on error
 */
extern time_t csem_get_otime(int sem_id);

#endif /* CSEM_H__ */
