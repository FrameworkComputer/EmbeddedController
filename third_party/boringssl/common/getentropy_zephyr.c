/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Implement getentropy (used by BoringSSL) using Zephyr Entropy Device. */

#include <errno.h>
#include <stddef.h>

#include <zephyr/drivers/entropy.h>
#include <zephyr/kernel.h>

#define rng DEVICE_DT_GET(DT_CHOSEN(zephyr_entropy))

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
	 * getentropy() uses size_t to represent buffer size, but Zephyr uses
	 * uint16_t. The length check above allows us to safely cast without
	 * overflow.
	 */
	if (entropy_get_entropy(rng, buffer, (uint16_t)length)) {
		errno = EIO;
		return -1;
	}

	return 0;
}
