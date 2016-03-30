/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* An implementation of HKDF as per RFC 5869. */

#include "dcrypto.h"
#include "internal.h"

static int hkdf_extract(uint8_t *PRK, const uint8_t *salt, size_t salt_len,
			const uint8_t *IKM, size_t IKM_len)
{
	struct HMAC_CTX ctx;

	if (PRK == NULL)
		return 0;
	if (salt == NULL && salt_len > 0)
		return 0;
	if (IKM == NULL && IKM_len > 0)
		return 0;

	dcrypto_HMAC_SHA256_init(&ctx, salt, salt_len);
	dcrypto_HMAC_update(&ctx, IKM, IKM_len);
	memcpy(PRK, dcrypto_HMAC_final(&ctx), SHA256_DIGEST_BYTES);
	return 1;
}

static int hkdf_expand(uint8_t *OKM, size_t OKM_len, const uint8_t *PRK,
		const uint8_t *info, size_t info_len)
{
	uint8_t count = 1;
	const uint8_t *T = OKM;
	size_t T_len = 0;
	uint32_t num_blocks = (OKM_len / SHA256_DIGEST_BYTES) +
		(OKM_len % SHA256_DIGEST_BYTES ? 1 : 0);

	if (OKM == NULL || OKM_len == 0)
		return 0;
	if (PRK == NULL)
		return 0;
	if (info == NULL && info_len > 0)
		return 0;
	if (num_blocks > 255)
		return 0;

	while (OKM_len > 0) {
		struct HMAC_CTX ctx;
		const size_t block_size = MIN(OKM_len, SHA256_DIGEST_BYTES);

		dcrypto_HMAC_SHA256_init(&ctx, PRK, SHA256_DIGEST_BYTES);
		dcrypto_HMAC_update(&ctx, T, T_len);
		dcrypto_HMAC_update(&ctx, info, info_len);
		dcrypto_HMAC_update(&ctx, &count, sizeof(count));
		memcpy(OKM, dcrypto_HMAC_final(&ctx), block_size);

		T += T_len;
		T_len = SHA256_DIGEST_BYTES;
		count += 1;
		OKM += block_size;
		OKM_len -= block_size;
	}
	return 1;
}

int DCRYPTO_hkdf(uint8_t *OKM, size_t OKM_len,
		const uint8_t *salt, size_t salt_len,
		const uint8_t *IKM, size_t IKM_len,
		const uint8_t *info, size_t info_len)
{
	int result;
	uint8_t PRK[SHA256_DIGEST_BYTES];

	if (!hkdf_extract(PRK, salt, salt_len, IKM, IKM_len))
		return 0;

	result = hkdf_expand(OKM, OKM_len, PRK, info, info_len);
	memset(PRK, 0, sizeof(PRK));
	return result;
}
