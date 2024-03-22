/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Implement the boringssl sysrand from EC TRNG. */

#include "openssl/base.h"
#include "trng.h"

#include <errno.h>

#include <unistd.h>

// We should define the getentropy for boringssl 24.
#if BORINGSSL_API_VERSION >= 24

// We don't want to conflict with the linux getentropy.
#if !defined(__linux__)
int getentropy(void *buffer, size_t length)
{
	if (!buffer) {
		errno = EFAULT;
		return -1;
	}

	if (length > 256) {
		errno = EIO;
		return -1;
	}

	trng_init();
	trng_rand_bytes(buffer, length);
	trng_exit();
	return 0;
}
#endif // !defined(__linux__)

// TDOD(b/273639386): Remove this after we uprev the boringssl.
#else

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

#endif // BORINGSSL_API_VERSION >= 24
