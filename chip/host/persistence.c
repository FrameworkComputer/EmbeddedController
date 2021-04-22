/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Persistence module for emulator */

/* This provides storage that can be opened, closed and reopened by the
 * current process at will, whose naming even remains stable across multiple
 * invocations of the same executable, while providing a unique name for
 * each executable (as determined by path) that uses these routines.
 *
 * Useful when semi-permanent storage is required even with many
 * similar processes running in parallel (e.g. in a highly parallel
 * test suite run.
 *
 * mkstemp and friends don't provide these properties which is why we have
 * this homegrown implementation of something similar-yet-different.
 */

#include <linux/limits.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include "util.h"

/* The longest path in a chroot seems to be about 280 characters (as of
 * April 2021) so define a cut-off instead of just hoping for the best:
 * If we were to run into a path that is nearly PATH_MAX bytes long,
 * file names could end up being reused inadvertedly because the various
 * snprintf calls would cut off the trailing characters, so the "tag" (and
 * maybe more) is gone even though it only exists for differentiation.
 *
 * Instead bail out if we encounter a path (to an executable using these
 * routines) that is longer than we expect.
 *
 * Round up for some spare room because why not?
 */
static const int max_len = 300;

/* This must be at least the size of the prefix added in get_storage_path */
static const int max_prefix_len = 25;

static void get_storage_path(char *out)
{
	char buf[PATH_MAX];
	int sz;
	char *current;

	sz = readlink("/proc/self/exe", buf, PATH_MAX - 1);
	buf[sz] = '\0';

	ASSERT(sz <= max_len);

	/* replace / by underscores in the path to get the shared memory name */
	current = strchr(buf, '/');
	while (current) {
		*current = '_';
		current = strchr(current, '/');
	}


	sz = snprintf(out, PATH_MAX - 1, "/dev/shm/EC_persist_%.*s",
		max_len, buf);
	out[PATH_MAX - 1] = '\0';

	ASSERT(sz <= max_len + max_prefix_len);
}

FILE *get_persistent_storage(const char *tag, const char *mode)
{
	char buf[PATH_MAX];
	char path[PATH_MAX];

	/* There's no longer tag in use right now, and there shouldn't be. */
	ASSERT(strlen(tag) < 32);

	/*
	 * The persistent storage with tag 'foo' for test 'bar' would
	 * be named 'bar_persist_foo'
	 */
	get_storage_path(buf);
	snprintf(path, PATH_MAX - 1, "%.*s_%32s",
		max_len + max_prefix_len, buf, tag);
	path[PATH_MAX - 1] = '\0';

	return fopen(path, mode);
}

void release_persistent_storage(FILE *ps)
{
	fclose(ps);
}

void remove_persistent_storage(const char *tag)
{
	char buf[PATH_MAX];
	char path[PATH_MAX];

	/* There's no longer tag in use right now, and there shouldn't be. */
	ASSERT(strlen(tag) < 32);

	get_storage_path(buf);
	snprintf(path, PATH_MAX - 1, "%.*s_%32s",
		max_len + max_prefix_len, buf, tag);
	path[PATH_MAX - 1] = '\0';

	unlink(path);
}
