/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "crypto_api.h"
#include "dcrypto.h"

void app_compute_hash(uint8_t *p_buf, size_t num_bytes,
		  uint8_t *p_hash, size_t hash_len)
{
	uint8_t sha1_digest[SHA_DIGEST_SIZE];

	/*
	 * Use the built in dcrypto engine to generate the sha1 hash of the
	 * buffer.
	 */
	DCRYPTO_SHA1_hash((uint8_t *)p_buf, num_bytes, sha1_digest);

	memcpy(p_hash, sha1_digest, MIN(hash_len, sizeof(sha1_digest)));

	if (hash_len > sizeof(sha1_digest))
		memset(p_hash + sizeof(sha1_digest), 0,
		       hash_len - sizeof(sha1_digest));
}

int app_cipher(const void *salt, void *out, const void *in, size_t size)
{
	return DCRYPTO_app_cipher(NVMEM, salt, out, in, size);
}

int crypto_enabled(void)
{
	return DCRYPTO_ladder_is_enabled();
}
