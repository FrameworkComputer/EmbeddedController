/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Mock TRNG driver for unit test.
 *
 * Although a TRNG is designed to be anything but predictable,
 * this implementation strives to be as predictable and defined
 * as possible to allow reproducing unit tests and fuzzer crashes.
 */

#ifndef TEST_BUILD
#error "This fake trng driver must not be used in non-test builds."
#endif

#include "common.h"

#include <stdint.h>
#include <stdlib.h> /* Only valid for host */

static unsigned int seed;

test_mockable void trng_init(void)
{
	seed = 0;
	srand(seed);
}

test_mockable void trng_exit(void)
{
}

test_mockable uint32_t trng_rand(void)
{
	return (uint32_t)rand_r(&seed);
}

test_mockable void trng_rand_bytes(void *buffer, size_t len)
{
	uint8_t *b, *end;

	for (b = buffer, end = b + len; b != end; b++)
		*b = (uint8_t)rand_r(&seed);
}
