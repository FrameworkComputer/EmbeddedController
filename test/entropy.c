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

static const int log2_mult = 2;

/*
 * log2 (multiplied by 2). For non-power of 2, this rounds to the closest
 * half-integer, otherwise the value is exact.
 */
uint32_t log2(int32_t val)
{
	int val1 = 31 - __builtin_clz(val);
	int val2 = 32 - __builtin_clz(val - 1);

	return log2_mult * (val1 + val2)/2;
}

void run_test(int argc, char **argv)
{
	const int loopcount = 512;

	uint8_t buffer[32];
	timestamp_t t0, t1;
	int i, j;
	uint32_t entropy;
	const int totalcount = loopcount * sizeof(buffer);
	const int log2totalcount = log2(totalcount);

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
			ccprintf("Got %zd bytes in %" PRId64 " us\n",
				sizeof(buffer), t1.val - t0.val);

		for (j = 0; j < sizeof(buffer); j++)
			buckets[buffer[j]]++;

		watchdog_reload();
	}

	ccprintf("Total count: %d\n", totalcount);
	ccprintf("Buckets: ");
	entropy = 0;
	for (j = 0; j < 256; j++) {
		/*
		 * Shannon entropy (base 2) is sum of -p[j] * log_2(p[j]).
		 * p[j] = buckets[j]/totalcount
		 * -p[j] * log_2(p[j])
		 *  = -(buckets[j]/totalcount) * log_2(buckets[j]/totalcount)
		 *  = buckets[j] * (log_2(totalcount) - log_2(buckets[j]))
		 *                                               / totalcount
		 * Our log2() function is scaled by log2_mult, and we defer the
		 * division by totalcount until we get the total sum, so we need
		 * to divide by (log2_mult * totalcount) at the end.
		 */
		entropy += buckets[j] * (log2totalcount - log2(buckets[j]));
		ccprintf("%d;", buckets[j]);
		cflush();
	}
	ccprintf("\n");

	ccprintf("Entropy: %u/1000 bits\n",
		entropy * 1000 / (log2_mult * totalcount));

	/* We want at least 2 bits of entropy (out of a maximum of 8) */
	if ((entropy / (log2_mult * totalcount)) >= 2)
		test_pass();
	else
		test_fail();
}
