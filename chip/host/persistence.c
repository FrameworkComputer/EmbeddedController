/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Persistence module for emulator */

#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define BUF_SIZE 1024

static void get_storage_path(char *out)
{
	char buf[BUF_SIZE];
	int sz;

	sz = readlink("/proc/self/exe", buf, BUF_SIZE);
	buf[sz] = '\0';
	if (snprintf(out, BUF_SIZE, "%s_persist", buf) >= BUF_SIZE)
		out[BUF_SIZE - 1] = '\0';
}

FILE *get_persistent_storage(const char *tag, const char *mode)
{
	char buf[BUF_SIZE];
	char path[BUF_SIZE];

	/*
	 * The persistent storage with tag 'foo' for test 'bar' would
	 * be named 'bar_persist_foo'
	 */
	get_storage_path(buf);
	if (snprintf(path, BUF_SIZE, "%s_%s", buf, tag) >= BUF_SIZE)
		path[BUF_SIZE - 1] = '\0';

	return fopen(path, mode);
}

void release_persistent_storage(FILE *ps)
{
	fclose(ps);
}

void remove_persistent_storage(const char *tag)
{
	char buf[BUF_SIZE];
	char path[BUF_SIZE];

	get_storage_path(buf);
	if (snprintf(path, BUF_SIZE, "%s_%s", buf, tag) >= BUF_SIZE)
		path[BUF_SIZE - 1] = '\0';

	unlink(path);
}
