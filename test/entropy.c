/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Tests entropy source.
 */

#include "console.h"
#include "common.h"
#include "rollback.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

static int buckets[256];

void run_test(void)
{
	const int loopcount = 512;

	uint8_t buffer[32];
	timestamp_t t0, t1;
	int i, j;

	memset(buckets, 0, sizeof(buckets));

	for (i = 0; i < loopcount; i++) {
		t0 = get_time();
		if (!board_get_entropy(buffer, sizeof(buffer))) {
			ccprintf("Cannot get entropy\n");
			test_fail();
			return;
		}
		t1 = get_time();
		if (i == 0)
			ccprintf("Got %d bytes in %ld us\n",
				sizeof(buffer), t1.val - t0.val);

		for (j = 0; j < sizeof(buffer); j++)
			buckets[buffer[j]]++;

		watchdog_reload();
	}

	ccprintf("Total count: %d\n", loopcount * sizeof(buffer));
	ccprintf("Buckets: ");
	for (j = 0; j < 256; j++) {
		ccprintf("%d;", buckets[j]);
		cflush();
	}
	ccprintf("\n");
	/*
	 * From the data above, entropy can be obtained with this command:
	 * tr ';' '\n' | awk 'BEGIN { e = 0; tot=16384.0 }
		{ p = $1/tot; if (p > 0) { e -= p*log(p)/log(2) } }
		END { print e }'
	 */
	test_pass();
}
