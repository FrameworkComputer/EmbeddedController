/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "panic.h"
#include "util.h"

void panic_assert_fail(const char *msg, const char *func, const char *fname,
		       int linenum)
{
	fprintf(stderr, "ASSERTION FAIL: %s:%d:%s - %s\n",
		fname, linenum, func, msg);
	fflush(stderr);

	puts("Fail!"); /* Inform test runner */
	fflush(stdout);

	exit(1);
}
