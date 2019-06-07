/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "aes.h"
#include "aes-gcm.h"
#include "fpsensor_crypto.h"
#include "fpsensor_private.h"
#include "fpsensor_state.h"
#include "rollback.h"
#include "sha256.h"

#if !defined(CONFIG_AES) || !defined(CONFIG_AES_GCM) || \
	!defined(CONFIG_ROLLBACK_SECRET_SIZE)
#error "fpsensor requires AES, AES_GCM and ROLLBACK_SECRET_SIZE"
#endif

int derive_encryption_key(uint8_t *out_key, uint8_t *salt)
{
	int ret;
	uint8_t key_buf[SHA256_DIGEST_SIZE];
	uint8_t prk[SHA256_DIGEST_SIZE];
	uint8_t message[sizeof(user_id) + 1];
	uint8_t ikm[CONFIG_ROLLBACK_SECRET_SIZE + sizeof(tpm_seed)];

	BUILD_ASSERT(SBP_ENC_KEY_LEN <= SHA256_DIGEST_SIZE);
	BUILD_ASSERT(SBP_ENC_KEY_LEN <= CONFIG_ROLLBACK_SECRET_SIZE);
	BUILD_ASSERT(sizeof(user_id) == SHA256_DIGEST_SIZE);

	if (!fp_tpm_seed_is_set()) {
		CPRINTS("Seed hasn't been set.");
		return EC_RES_ERROR;
	}

	/*
	 * The first CONFIG_ROLLBACK_SECRET_SIZE bytes of IKM are read from the
	 * anti-rollback blocks.
	 */
	ret = rollback_get_secret(ikm);
	if (ret != EC_SUCCESS) {
		CPRINTS("Failed to read rollback secret: %d", ret);
		return EC_RES_ERROR;
	}
	/*
	 * IKM is the concatenation of the rollback secret and the seed from
	 * the TPM.
	 */
	memcpy(ikm + CONFIG_ROLLBACK_SECRET_SIZE, tpm_seed, sizeof(tpm_seed));

	/*
	 * Derive a key with the "extract" step of HKDF
	 * https://tools.ietf.org/html/rfc5869#section-2.2
	 */
	hmac_SHA256(prk, salt, FP_CONTEXT_SALT_BYTES, ikm, sizeof(ikm));
	memset(ikm, 0, sizeof(ikm));

	/*
	 * Only 1 "expand" step of HKDF since the size of the "info" context
	 * (user_id in our case) is exactly SHA256_DIGEST_SIZE.
	 * https://tools.ietf.org/html/rfc5869#section-2.3
	 */
	memcpy(message, user_id, sizeof(user_id));
	/* 1 step, set the counter byte to 1. */
	message[sizeof(message) - 1] = 0x01;
	hmac_SHA256(key_buf, prk, sizeof(prk), message, sizeof(message));
	memset(prk, 0, sizeof(prk));

	memcpy(out_key, key_buf, SBP_ENC_KEY_LEN);
	memset(key_buf, 0, sizeof(key_buf));

	return EC_RES_SUCCESS;
}

int aes_gcm_encrypt(const uint8_t *key, int key_size,
		    const uint8_t *plaintext,
		    uint8_t *ciphertext, int text_size,
		    const uint8_t *nonce, int nonce_size,
		    uint8_t *tag, int tag_size)
{
	int res;
	AES_KEY aes_key;
	GCM128_CONTEXT ctx;

	if (nonce_size != FP_CONTEXT_NONCE_BYTES) {
		CPRINTS("Invalid nonce size %d bytes", nonce_size);
		return EC_RES_INVALID_PARAM;
	}

	res = AES_set_encrypt_key(key, 8 * key_size, &aes_key);
	if (res) {
		CPRINTS("Failed to set encryption key: %d", res);
		return EC_RES_ERROR;
	}
	CRYPTO_gcm128_init(&ctx, &aes_key, (block128_f)AES_encrypt, 0);
	CRYPTO_gcm128_setiv(&ctx, &aes_key, nonce, nonce_size);
	/* CRYPTO functions return 1 on success, 0 on error. */
	res = CRYPTO_gcm128_encrypt(&ctx, &aes_key, plaintext, ciphertext,
				    text_size);
	if (!res) {
		CPRINTS("Failed to encrypt: %d", res);
		return EC_RES_ERROR;
	}
	CRYPTO_gcm128_tag(&ctx, tag, tag_size);
	return EC_RES_SUCCESS;
}

int aes_gcm_decrypt(const uint8_t *key, int key_size, uint8_t *plaintext,
		    const uint8_t *ciphertext, int text_size,
		    const uint8_t *nonce, int nonce_size,
		    const uint8_t *tag, int tag_size)
{
	int res;
	AES_KEY aes_key;
	GCM128_CONTEXT ctx;

	if (nonce_size != FP_CONTEXT_NONCE_BYTES) {
		CPRINTS("Invalid nonce size %d bytes", nonce_size);
		return EC_RES_INVALID_PARAM;
	}

	res = AES_set_encrypt_key(key, 8 * key_size, &aes_key);
	if (res) {
		CPRINTS("Failed to set decryption key: %d", res);
		return EC_RES_ERROR;
	}
	CRYPTO_gcm128_init(&ctx, &aes_key, (block128_f)AES_encrypt, 0);
	CRYPTO_gcm128_setiv(&ctx, &aes_key, nonce, nonce_size);
	/* CRYPTO functions return 1 on success, 0 on error. */
	res = CRYPTO_gcm128_decrypt(&ctx, &aes_key, ciphertext, plaintext,
				    text_size);
	if (!res) {
		CPRINTS("Failed to decrypt: %d", res);
		return EC_RES_ERROR;
	}
	res = CRYPTO_gcm128_finish(&ctx, tag, tag_size);
	if (!res) {
		CPRINTS("Found incorrect tag: %d", res);
		return EC_RES_ERROR;
	}
	return EC_RES_SUCCESS;
}
