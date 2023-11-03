/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test some logic of the HyperDebug board.
 */

#include "board/hyperdebug/board_util.h"
#include "test_util.h"

#define SUBTEST(E)                  \
	do {                        \
		if ((ret = (E)))    \
			return ret; \
	} while (false)

static const uint32_t base_frequencies[3] = {
	1000,
	100000,
	150000,
};

/*
 * Invoke calculation of SPI clock divisor and base frequency, using three
 * artificial choices of base frequency: 1kHz, 100kHz and 150kHz.
 */
static int test_divisor(uint32_t desired_freq, uint32_t expected_freq,
			int expected_divisor, uint32_t expected_base_frequency)
{
	uint8_t divisor;
	size_t base_frequency_index;
	find_best_divisor(desired_freq, base_frequencies, 3, &divisor,
			  &base_frequency_index);
	ccprintf("Frequency %d: %d / %d\n", desired_freq,
		 base_frequencies[base_frequency_index], divisor + 1);
	uint32_t actual_freq =
		base_frequencies[base_frequency_index] / (divisor + 1);
	TEST_EQ(actual_freq, expected_freq, "%d");
	TEST_EQ(divisor + 1, expected_divisor, "%d");
	TEST_EQ(base_frequencies[base_frequency_index], expected_base_frequency,
		"%u");
	return EC_SUCCESS;
}

test_static int run_test_divisors(void)
{
	int ret;
	/* Frequency of 100 can be hit exactly as 1000 / 10 */
	SUBTEST(test_divisor(100, 100, 10, 1000));
	/* Frequency of 2000 can be hit exactly as 100000 / 50 */
	SUBTEST(test_divisor(2000, 2000, 50, 100000));
	/* Frequency of 30000 can be hit exactly as 150000 / 5 */
	SUBTEST(test_divisor(30000, 30000, 5, 150000));
	/* Frequency of 34000 can best be approximated by 100000 / 3 */
	SUBTEST(test_divisor(34000, 33333, 3, 100000));
	/* Frequency of 80000 can best be approximated as 150000 / 2 */
	SUBTEST(test_divisor(80000, 75000, 2, 150000));
	/*
	 * Frequency of 333 cannot be hit exactly, we allow slightly exceeding
	 * by a third of a Hertz, which will read back as 333 when querying the
	 * speed.
	 *
	 * It would be weird if a user requested 340 Hz and saw that HyperDebug
	 * gave the slightly lower 333 Hz instead, and then later requested 333
	 * Hz, only do be given an even lower speed at that point.  That would
	 * have been the experience, if HyperDebug had refused to exceed by a
	 * fraction of a Hertz.
	 */
	SUBTEST(test_divisor(333, 333, 3, 1000));
	/*
	 * Requested frequency is below the range, expect slowest possible
	 * setting.
	 */
	SUBTEST(test_divisor(1, 3, 256, 1000));
	/*
	 * Requested frequency is above the range, expect fastest possible
	 * setting.
	 */
	SUBTEST(test_divisor(1000000, 150000, 1, 150000));

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();
	RUN_TEST(run_test_divisors);
	test_print_result();
}
