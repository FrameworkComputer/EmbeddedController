/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "cts_common.h"

__attribute__((weak)) void clean_state(void)
{
	/* Each test overrides as needed */
}

void cts_main_loop(const struct cts_test* tests, const char *name)
{
	enum cts_rc rc;
	int i;

	cflush();
	for (i = 0; i < cts_test_count; i++) {
		CPRINTF("\n%s start\n", tests[i].name);
		cflush();
		clean_state();
		sync();
		rc = tests[i].run();
		CPRINTF("\n%s end %d\n", tests[i].name, rc);
		cflush();
	}

	CPRINTS("%s test suite finished", name);
}
