/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Implement getentropy (used by BoringSSL) using the EC TRNG. */

#include "trng.h"

#include <errno.h>
#include <unistd.h>

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
