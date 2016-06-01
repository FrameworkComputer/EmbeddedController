/* Copyright 2016, Google Inc.
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
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * file_lock.c: Implementation for a binary semaphore using a file lock.
 *
 * Warning: This relies on flock() which is known to be broken on NFS.
 *
 * The file will remain persistent once the lock has been used. Unfortunately,
 * unlinking the file can introduce a race condition so we leave the file
 * in place.
 *
 * The current process's PID will be written to the file for debug purposes.
 */

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "android.h"
#include "ipc_lock.h"
#include "locks.h"

#define SLEEP_INTERVAL_MS	50

static void msecs_to_timespec(int msecs, struct timespec *tmspec)
{
	tmspec->tv_sec = msecs / 1000;
	tmspec->tv_nsec = (msecs % 1000) * 1000 * 1000;
}

static int lock_is_held(struct ipc_lock *lock)
{
	return lock->is_held;
}

static int test_dir(const char *path)
{
	struct stat s;

	if (lstat(path, &s) < 0) {
		fprintf(stderr, "Cannot stat %s.\n", path);
		return -1;
	}

	if (!S_ISDIR(s.st_mode)) {
		fprintf(stderr, "%s is not a directory.\n", path);
		return -1;
	}

	return 0;
}

static int file_lock_open_or_create(struct ipc_lock *lock)
{
	char path[PATH_MAX];

	if (in_android()) {
		char *tmpdir;

		tmpdir = android_tmpdir_path();
		if (!tmpdir)
			return -1;

		if (snprintf(path, sizeof(path), "%s/%s",
				tmpdir, lock->filename) < 0) {
			return -1;
		}
	} else {
		const char *dir = SYSTEM_LOCKFILE_DIR;
		const char fallback[] = "/tmp";

		if (test_dir(dir)) {
			dir = fallback;
			fprintf(stderr, "Trying fallback directory: %s\n", dir);
			if (test_dir(dir))
				return -1;
		}

		if (snprintf(path, sizeof(path),
			"%s/%s", dir, lock->filename) < 0)
			return -1;

	}

	lock->fd = open(path, O_RDWR | O_CREAT, 0600);
	if (lock->fd < 0) {
		fprintf(stderr, "Cannot open lockfile %s", path);
		return -1;
	}

	return 0;
}

static int file_lock_get(struct ipc_lock *lock, int timeout_msecs)
{
	int msecs_remaining = timeout_msecs;
	struct timespec sleep_interval, rem;
	int ret = -1;

	if (timeout_msecs == 0)
		return flock(lock->fd, LOCK_EX | LOCK_NB);

	msecs_to_timespec(SLEEP_INTERVAL_MS, &sleep_interval);

	while ((ret = flock(lock->fd, LOCK_EX | LOCK_NB)) != 0) {
		if (errno != EWOULDBLOCK) {
			fprintf(stderr, "Error obtaining lock");
			return -1;
		}

		if (msecs_remaining < SLEEP_INTERVAL_MS)
			msecs_to_timespec(msecs_remaining, &sleep_interval);

		while (nanosleep(&sleep_interval, &rem) != 0) {
			if (errno == EINTR) {
				sleep_interval = rem;
				continue;
			} else {
				fprintf(stderr, "nanosleep() failed");
				return ret;
			}
		}

		if (timeout_msecs < 0)
			continue;

		msecs_remaining -= SLEEP_INTERVAL_MS;
		if (msecs_remaining < 0)
			break;
	}

	if (ret != 0) {
		fprintf(stderr, "Timed out waiting for file lock.\n");
		return -1;
	}

	return 0;
}

static int file_lock_write_pid(struct ipc_lock *lock)
{
	ssize_t len;
	/*
         * PIDs are usually 5 digits, but we'll reserve enough room for
	 * a value of 2^32 (10 digits) out of paranoia.
         */
	char pid_str[11];

	if (ftruncate(lock->fd, 0) < 0) {
		fprintf(stderr, "Cannot truncate lockfile");
		return -1;
	}

	snprintf(pid_str, sizeof(pid_str), "%lu", (unsigned long)getpid());
	len = write(lock->fd, pid_str, strlen(pid_str));
	if (len < 0) {
		fprintf(stderr, "Cannot write PID to lockfile");
		return -1;
	}

	return 0;
}

static void file_lock_release(struct ipc_lock *lock)
{
	if (flock(lock->fd, LOCK_UN) < 0)
		fprintf(stderr, "Cannot release lock");

	if (close(lock->fd) < 0)
		fprintf(stderr, "Cannot close lockfile");
}

/*
 * timeout <0 = no timeout (try forever)
 * timeout 0  = do not wait (return immediately)
 * timeout >0 = wait up to $timeout milliseconds
 *
 * returns 0 to indicate lock acquired
 * returns >0 to indicate lock was already held
 * returns <0 to indicate failed to acquire lock
 */
int acquire_lock(struct ipc_lock *lock, int timeout_msecs)
{
	/* check if it is already held */
	if (lock_is_held(lock))
		return 1;

	if (file_lock_open_or_create(lock))
		return -1;

	if (file_lock_get(lock, timeout_msecs)) {
		lock->is_held = 0;
		close(lock->fd);
		return -1;
	} else {
		lock->is_held = 1;
	}

	/*
	 * Write PID to lockfile for debug purposes. Failure to write to
	 * the file should not be considered fatal. There might be something
	 * bad happening with the filesystem, but the lock has already been
	 * obtained and we may need our tools for diagnostics and repairs
	 * so we should continue anyway.
	 */
	file_lock_write_pid(lock);
	return 0;
}

/*
 * returns 0 if lock was released successfully
 * returns -1 if lock had not been held before the call
 */
int release_lock(struct ipc_lock *lock)
{
	if (lock_is_held(lock)) {
		file_lock_release(lock);
		lock->is_held = 0;
		return 0;
	}

	return -1;
}
