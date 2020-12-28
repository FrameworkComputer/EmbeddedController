/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test utilities.
 */

#include "test_util.h"

/* Linear congruential pseudo random number generator */
uint32_t prng(uint32_t seed)
{
	return 22695477 * seed + 1;
}

uint32_t prng_no_seed(void)
{
	static uint32_t seed = 0x1234abcd;
	return seed = prng(seed);
}
