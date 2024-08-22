/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "timer.h"

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(unaligned_access_benchmark_hw_test, LOG_LEVEL_INF);

ZTEST_SUITE(unaligned_access_benchmark, NULL, NULL, NULL, NULL, NULL);

ZTEST(unaligned_access_benchmark, test_benchmark_unaligned_access)
{
	int i;
	timestamp_t t0, t1, t2, t3;
	alignas(uint32_t) volatile int8_t dst[2 * sizeof(uint32_t)];
	volatile uint32_t *const unaligned = (volatile uint32_t *)(dst + 1);
	volatile uint32_t *const aligned = (volatile uint32_t *)dst;

	LOG_INF("dst=%p", dst);
	LOG_INF("unaligned=%p and aligned=%p", unaligned, aligned);

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
	LOG_INF("Unaligned took %" PRId64 "us, which is %" PRId64
		"ns per iteration.",
		unaligned_time,
		(NSEC_PER_USEC * unaligned_time) / (iteration * unrollcount));

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
	LOG_INF("Aligned took %" PRId64 "us, which is %" PRId64
		"ns per iteration.",
		aligned_time,
		(NSEC_PER_USEC * aligned_time) / (iteration * unrollcount));

	LOG_INF("Unaligned write is %" PRId64 "%% slower that aligned.",
		(100 * unaligned_time) / aligned_time - 100);
}
