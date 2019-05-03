/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <openssl/evp.h>

#define HIDE_EC_STDLIB

#include "dcrypto.h"
#include "registers.h"

int DCRYPTO_aes_ctr(uint8_t *out, const uint8_t *key, uint32_t key_bits,
		const uint8_t *iv, const uint8_t *in, size_t in_len)
{
	EVP_CIPHER_CTX *ctx;
	int ret = 0;
	int out_len = 0;

	ctx = EVP_CIPHER_CTX_new();
	if (!ctx)
		return 0;

	if (EVP_EncryptInit_ex(ctx, EVP_aes_256_ctr(), NULL, key, iv) != 1)
		goto cleanup;

	if (EVP_EncryptUpdate(ctx, out, &out_len, in, in_len) != 1)
		goto cleanup;

	if (EVP_EncryptFinal(ctx, out + out_len, &out_len) != 1)
		goto cleanup;
	ret = 1;

cleanup:
	EVP_CIPHER_CTX_free(ctx);
	return ret;
}
