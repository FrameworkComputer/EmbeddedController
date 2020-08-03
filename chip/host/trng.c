
/* Copyright 2019 The Chromium OS Authors. All rights reserved.
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

#include <stdint.h>
#include <stdlib.h> /* Only valid for host */

#include "common.h"

static unsigned int seed;

test_mockable void init_trng(void)
{
	seed = 0;
	srand(seed);
}

test_mockable void exit_trng(void)
{
}

test_mockable void rand_bytes(void *buffer, size_t len)
{
	uint8_t *b, *end;

	for (b = buffer, end = b+len; b != end; b++)
		*b = (uint8_t)rand_r(&seed);
}
