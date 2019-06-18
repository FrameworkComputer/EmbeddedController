/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdio.h>
#include <stdlib.h>

#include "stack_trace.h"

void panic_assert_fail(const char *msg, const char *func, const char *fname,
		       int linenum)
{
	fprintf(stderr, "ASSERTION FAIL: %s:%d:%s - %s\n",
		fname, linenum, func, msg);
	task_dump_trace();

	puts("Fail!"); /* Inform test runner */
	fflush(stdout);

	exit(1);
}
