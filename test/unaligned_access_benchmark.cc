
/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "shared_mem.h"
#include "test_util.h"
#include "timer.h"

test_static int benchmark_unaligned_access()
{
	int i;
	timestamp_t t0, t1, t2, t3;
	alignas(uint32_t) volatile int8_t dst[2 * sizeof(uint32_t)];
	volatile uint32_t *const unaligned = (volatile uint32_t *)(dst + 1);
	volatile uint32_t *const aligned = (volatile uint32_t *)dst;

	ccprintf("dst=0x%p\n", dst);
	ccprintf("unaligned=0x%p and aligned=0x%p\n", unaligned, aligned);

	const int iteration = 1000000;
	const int unrollcount = 20;

	for (i = 0; i < sizeof(dst); ++i) {
		dst[i] = 0;
	}

	t0 = get_time();
	for (i = 0; i < iteration; ++i) {
		/* unaligned */
		// Our little write operation is much less significant than
		// the loop variable updating, so we need to unroll the loop a
		// a bit to make each loop action more significant.

		*unaligned = 0xFEF8F387;
		*unaligned = 0xFEF8F387;
		*unaligned = 0xFEF8F387;
		*unaligned = 0xFEF8F387;
		*unaligned = 0xFEF8F387;
		*unaligned = 0xFEF8F387;
		*unaligned = 0xFEF8F387;
		*unaligned = 0xFEF8F387;
		*unaligned = 0xFEF8F387;
		*unaligned = 0xFEF8F387;
		*unaligned = 0xFEF8F387;
		*unaligned = 0xFEF8F387;
		*unaligned = 0xFEF8F387;
		*unaligned = 0xFEF8F387;
		*unaligned = 0xFEF8F387;
		*unaligned = 0xFEF8F387;
		*unaligned = 0xFEF8F387;
		*unaligned = 0xFEF8F387;
		*unaligned = 0xFEF8F387;
		*unaligned = 0xFEF8F387;
	}
	t1 = get_time();
	uint64_t unaligned_time = t1.val - t0.val;
	ccprintf("Unaligned took %" PRId64 "us, which is %" PRId64
		 "ns per iteration.\n",
		 unaligned_time,
		 (1000 * unaligned_time) / (iteration * unrollcount));

	for (i = 0; i < sizeof(dst); ++i) {
		dst[i] = 0;
	}

	t2 = get_time();
	for (i = 0; i < iteration; ++i) {
		/* aligned */
		*aligned = 0xFEF8F387;
		*aligned = 0xFEF8F387;
		*aligned = 0xFEF8F387;
		*aligned = 0xFEF8F387;
		*aligned = 0xFEF8F387;
		*aligned = 0xFEF8F387;
		*aligned = 0xFEF8F387;
		*aligned = 0xFEF8F387;
		*aligned = 0xFEF8F387;
		*aligned = 0xFEF8F387;
		*aligned = 0xFEF8F387;
		*aligned = 0xFEF8F387;
		*aligned = 0xFEF8F387;
		*aligned = 0xFEF8F387;
		*aligned = 0xFEF8F387;
		*aligned = 0xFEF8F387;
		*aligned = 0xFEF8F387;
		*aligned = 0xFEF8F387;
		*aligned = 0xFEF8F387;
		*aligned = 0xFEF8F387;
	}
	t3 = get_time();
	uint64_t aligned_time = t3.val - t2.val;
	ccprintf("Aligned took %" PRId64 "us, which is %" PRId64
		 "ns per iteration.\n",
		 aligned_time,
		 (1000 * aligned_time) / (iteration * unrollcount));

	ccprintf("Unaligned write is %" PRId64 "%% slower that aligned.\n",
		 (100 * unaligned_time) / aligned_time - 100);
	return EC_SUCCESS;
}

void run_test(int, const char **)
{
	test_reset();
	RUN_TEST(benchmark_unaligned_access);
	test_print_result();
}
