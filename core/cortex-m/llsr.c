/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Enable the use of right shift for uint64_t. */

#include <console.h>
#include <compile_time_macros.h>
#include <stdint.h>

union words {
	uint64_t u64;
	uint32_t w[2];
};

uint64_t __attribute__((used)) __aeabi_llsr(uint64_t v, uint32_t shift)
{
	union words val;
	union words res;

	val.u64 = v;
	res.w[1] = val.w[1] >> shift;
	res.w[0] = val.w[0] >> shift;
	res.w[0] |= val.w[1] >> (shift - 32); /* Handle shift >= 32*/
	res.w[0] |= val.w[1] << (32 - shift); /* Handle shift <= 32*/
	return res.u64;
}

#ifdef CONFIG_LLSR_TEST

static int command_llsr(int argc, char **argv)
{
	/* Volatile to prevent compilier optimization from interfering. */
	volatile uint64_t start = 0x123456789ABCDEF0ull;
	uint32_t x;

	const struct {
		uint32_t shift_by;
		uint64_t result;
	} cases[] = {
		{0, start},
		{16, 0x123456789ABCull},
		{32, 0x12345678u},
		{48, 0x1234u},
		{64, 0u}
	};

	for (x = 0; x < ARRAY_SIZE(cases); ++x) {
		if ((start >> cases[x].shift_by) != cases[x].result) {
			ccprintf("FAILED %d\n", cases[x].shift_by);
			return EC_ERROR_UNKNOWN;
		}
	}

	ccprintf("SUCCESS\n");
	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(
		llsrtest, command_llsr,
		"",
		"Run tests against the LLSR ABI. Prints SUCCESS or FAILURE.");

#endif  /* CONFIG_LLSR_TEST */
