/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "dcrypto.h"
#include "util.h"

void app_compute_hash(uint8_t *p_buf, size_t num_bytes,
		      uint8_t *p_hash, size_t hash_len)
{
	uint8_t digest[SHA256_DIGEST_SIZE];

	/*
	 * Use the built in dcrypto engine to generate the sha1 hash of the
	 * buffer.
	 */
	DCRYPTO_SHA256_hash((uint8_t *)p_buf, num_bytes, digest);

	memcpy(p_hash, digest, MIN(hash_len, sizeof(digest)));

	if (hash_len > sizeof(digest))
		memset(p_hash + sizeof(digest), 0,
		       hash_len - sizeof(digest));
}

int app_cipher(const void *salt_p, void *out_p, const void *in_p, size_t size)
{
	/* See README.md for why this is not a real encryption.. */
	size_t i;
	const uint8_t *src;
	const uint8_t *salt;
	uint8_t *dst;

	src = in_p;
	salt = salt_p;
	dst = out_p;

	for (i = 0; i < size; i++)
		dst[i] = src[i] ^ salt[i & 7];

	return 1;
}

int crypto_enabled(void)
{
	return 1;
}
