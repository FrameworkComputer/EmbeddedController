/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Implement the BoringSSL sysrand from Zephyr Entropy Device. */

#include <errno.h>

#include <zephyr/drivers/entropy.h>
#include <zephyr/kernel.h>

#include <../crypto/fipsmodule/rand/internal.h>
#include <openssl/base.h>

#define rng DEVICE_DT_GET(DT_CHOSEN(zephyr_entropy))

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

	if (!device_is_ready(rng)) {
		errno = EIO;
		return -1;
	}

	/*
	 * BoringSSL uses size_t to represent buffer size, but Zephyr uses
	 * uint16_t. The length check above allows us to safely cast without
	 * overflow.
	 */
	if (entropy_get_entropy(rng, buffer, (uint16_t)length)) {
		errno = EIO;
		return -1;
	}

	return 0;
}
#endif // !defined(__linux__)
