/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Implement the BoringSSL sysrand from Zephyr Entropy Device. */

#include <zephyr/drivers/entropy.h>
#include <zephyr/kernel.h>

#include <../crypto/fipsmodule/rand/internal.h>

#define rng DEVICE_DT_GET(DT_CHOSEN(zephyr_entropy))

void CRYPTO_sysrand(uint8_t *out, size_t requested)
{
	/*
	 * BoringSSL uses size_t to represent buffer size, but Zephyr uses
	 * uint16_t. Crash the system if user requested more than UINT16_MAX
	 * bytes.
	 */
	if (!device_is_ready(rng) || requested > UINT16_MAX)
		k_oops();

	if (entropy_get_entropy(rng, out, (uint16_t)requested))
		k_oops();
}

void CRYPTO_sysrand_for_seed(uint8_t *out, size_t requested)
{
	return CRYPTO_sysrand(out, requested);
}
