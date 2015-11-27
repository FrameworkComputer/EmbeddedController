/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "CryptoEngine.h"

#include "util.h"
#include "dcrypto.h"

static const HASH_INFO *lookup_hash_info(TPM_ALG_ID alg)
{
	int i;
	const int num_algs = ARRAY_SIZE(g_hashData);

	for (i = 0; i < num_algs - 1; i++) {
		if (g_hashData[i].alg == alg)
			return &g_hashData[i];
	}
	return &g_hashData[num_algs - 1];
}

TPM_ALG_ID _cpri__GetContextAlg(CPRI_HASH_STATE *hash_state)
{
	return hash_state->hashAlg;
}

TPM_ALG_ID _cpri__GetHashAlgByIndex(uint32_t index)
{
	if (index >= HASH_COUNT)
		return TPM_ALG_NULL;
	return g_hashData[index].alg;
}

uint16_t _cpri__GetDigestSize(TPM_ALG_ID alg)
{
	return lookup_hash_info(alg)->digestSize;
}

uint16_t _cpri__GetHashBlockSize(TPM_ALG_ID alg)
{
	return lookup_hash_info(alg)->blockSize;
}

BUILD_ASSERT(sizeof(CPRI_HASH_STATE) == sizeof(EXPORT_HASH_STATE));
void _cpri__ImportExportHashState(CPRI_HASH_STATE *osslFmt,
				EXPORT_HASH_STATE *externalFmt,
				IMPORT_EXPORT direction)
{
	if (direction == IMPORT_STATE)
		memcpy(osslFmt, externalFmt, sizeof(CPRI_HASH_STATE));
	else
		memcpy(externalFmt, osslFmt, sizeof(CPRI_HASH_STATE));
}

uint16_t _cpri__HashBlock(TPM_ALG_ID alg, uint32_t in_len, uint8_t *in,
			uint32_t out_len, uint8_t *out)
{
	uint8_t digest[SHA_DIGEST_MAX_BYTES];
	const uint16_t digest_len = _cpri__GetDigestSize(alg);

	if (digest_len == 0)
		return 0;

	switch (alg) {
	case TPM_ALG_SHA1:
		DCRYPTO_SHA1_hash(in, in_len, digest);
		break;

	case TPM_ALG_SHA256:
		DCRYPTO_SHA256_hash(in, in_len, digest);
		break;
/* TODO: add support for SHA384 and SHA512
 *
 *	case TPM_ALG_SHA384:
 *		DCRYPTO_SHA384_hash(in, in_len, p);
 *		break;
 *	case TPM_ALG_SHA512:
 *		DCRYPTO_SHA512_hash(in, in_len, p);
 *		break; */
	default:
		FAIL(FATAL_ERROR_INTERNAL);
		break;
	}

	out_len = MIN(out_len, digest_len);
	memcpy(out, digest, out_len);
	return out_len;
}

BUILD_ASSERT(sizeof(struct HASH_CTX) <=
	     sizeof(((CPRI_HASH_STATE *)0)->state));
uint16_t _cpri__StartHash(TPM_ALG_ID alg, BOOL sequence,
			  CPRI_HASH_STATE *state)
{
	struct HASH_CTX *ctx = (struct HASH_CTX *) state->state;
	uint16_t result;

	switch (alg) {
	case TPM_ALG_SHA1:
		DCRYPTO_SHA1_init(ctx, sequence);
		result = DCRYPTO_HASH_size(ctx);
		break;
	case TPM_ALG_SHA256:
		DCRYPTO_SHA256_init(ctx, sequence);
		result = DCRYPTO_HASH_size(ctx);
		break;
/* TODO: add support for SHA384 and SHA512
 *	case TPM_ALG_SHA384:
 *		DCRYPTO_SHA384_init(in, in_len, p);
 *		break;
 *	case TPM_ALG_SHA512:
 *		DCRYPTO_SHA512_init(in, in_len, p);
 *		break; */
	default:
		result = 0;
		break;
	}

	return result;
}

void _cpri__UpdateHash(CPRI_HASH_STATE *state, uint32_t in_len,
		BYTE *in)
{
	struct HASH_CTX *ctx = (struct HASH_CTX *) state->state;

	DCRYPTO_HASH_update(ctx, in, in_len);
}

uint16_t _cpri__CompleteHash(CPRI_HASH_STATE *state,
			uint32_t out_len, uint8_t *out)
{
	struct HASH_CTX *ctx = (struct HASH_CTX *) state->state;

	out_len = MIN(DCRYPTO_HASH_size(ctx), out_len);
	memcpy(out, DCRYPTO_HASH_final(ctx), out_len);
	return out_len;
}
