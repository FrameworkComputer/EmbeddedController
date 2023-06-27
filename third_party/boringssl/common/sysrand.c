/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Implement the boringssl sysrand from EC TRNG. */

#include "trng.h"

void CRYPTO_sysrand(uint8_t *out, size_t requested)
{
	trng_init();
	trng_rand_bytes(out, requested);
	trng_exit();
}

void CRYPTO_sysrand_for_seed(uint8_t *out, size_t requested)
{
	return CRYPTO_sysrand(out, requested);
}
