/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "internal.h"
#include "dcrypto.h"

#include "trng.h"
#include "util.h"

#include "cryptoc/p256.h"
#include "cryptoc/sha256.h"

#define AES_KEY_BYTES  16
#define HMAC_KEY_BYTES 32

#define AES_BLOCK_BYTES 16

/* P256 based hybrid encryption.  The output format is:
 *
 *   0x04 || PUBKEY || AUTH_DATA || AES128_CTR(PLAINTEXT) ||
 *      HMAC_SHA256(AUTH_DATA || CIPHERTEXT)
 */
size_t DCRYPTO_ecies_encrypt(
	void *out, size_t out_len, const void *in, size_t in_len,
	size_t auth_data_len, const uint8_t *iv,
	const p256_int *pub_x, const p256_int *pub_y,
	const uint8_t *salt, size_t salt_len,
	const uint8_t *info, size_t info_len)
{
	p256_int eph_d;
	p256_int eph_x;
	p256_int eph_y;
	uint8_t seed[P256_NBYTES];
	p256_int secret_x;
	p256_int secret_y;
	/* Key bytes to be extracted from HKDF. */
	uint8_t key[AES_KEY_BYTES + HMAC_KEY_BYTES];
	const uint8_t *aes_key;
	const uint8_t *hmac_key;
	LITE_HMAC_CTX ctx;
	uint8_t *outp = out;
	uint8_t *ciphertext;

	if (auth_data_len > in_len)
		return 0;
	if (out_len < 1 + P256_NBYTES + P256_NBYTES +
		in_len + SHA256_DIGEST_SIZE)
		return 0;

	/* Generate emphemeral EC key. */
	rand_bytes(seed, sizeof(seed));
	if (!DCRYPTO_p256_key_from_bytes(&eph_x, &eph_y, &eph_d, seed))
		return 0;
	/* Compute DH point. */
	if (!DCRYPTO_p256_point_mul(&secret_x, &secret_y,
					&eph_d, pub_x, pub_y))
		return 0;
	/* Check for computational errors. */
	if (!dcrypto_p256_is_valid_point(&secret_x, &secret_y))
		return 0;
	/* Convert secret to big-endian. */
	reverse(&secret_x, sizeof(secret_x));
	/* Derive shared secret. */
	if (!DCRYPTO_hkdf(key, sizeof(key), salt, salt_len,
				(uint8_t *) &secret_x, sizeof(secret_x),
				info, info_len))
		return 0;

	aes_key = &key[0];
	hmac_key = &key[AES_KEY_BYTES];

	if (out == in)
		ciphertext = out + auth_data_len;    /* In place encrypt. */
	else
		ciphertext = out + 1 + P256_NBYTES + P256_NBYTES +
			auth_data_len;

	/* Compute ciphertext. */
	if (!DCRYPTO_aes_ctr(ciphertext, aes_key, AES_KEY_BYTES * 8, iv,
				in + auth_data_len, in_len - auth_data_len))
		return 0;

	/* Write out auth_data / ciphertext. */
	outp = out + 1 + P256_NBYTES + P256_NBYTES;
	if (out == in)
		memmove(outp, in, in_len);
	else
		memcpy(outp, in, auth_data_len);

	/* Write out ephemeral pub key. */
	outp = out;
	*outp++ = 0x04;  /* uncompressed EC public key. */
	p256_to_bin(&eph_x, outp);
	outp += P256_NBYTES;
	p256_to_bin(&eph_y, outp);
	outp += P256_NBYTES;

	/* Calculate HMAC(auth_data || ciphertext). */
	DCRYPTO_HMAC_SHA256_init(&ctx, hmac_key, HMAC_KEY_BYTES);
	HASH_update(&ctx.hash, outp, in_len);
	outp += in_len;
	memcpy(outp, DCRYPTO_HMAC_final(&ctx), SHA256_DIGEST_SIZE);
	outp += SHA256_DIGEST_SIZE;

	return outp - (uint8_t *) out;
}

size_t DCRYPTO_ecies_decrypt(
	void *out, size_t out_len, const void *in, size_t in_len,
	size_t auth_data_len, const uint8_t *iv,
	const p256_int *d,
	const uint8_t *salt, size_t salt_len,
	const uint8_t *info, size_t info_len)
{
	p256_int eph_x;
	p256_int eph_y;
	p256_int secret_x;
	p256_int secret_y;
	uint8_t key[AES_KEY_BYTES + HMAC_KEY_BYTES];
	const uint8_t *aes_key;
	const uint8_t *hmac_key;
	LITE_HMAC_CTX ctx;
	const uint8_t *inp = in;
	uint8_t *outp = out;

	if (in_len < 1 + P256_NBYTES + P256_NBYTES + auth_data_len +
		SHA256_DIGEST_SIZE)
		return 0;
	if (inp[0] != 0x04)
		return 0;

	in_len -= 1 + P256_NBYTES + P256_NBYTES + SHA256_DIGEST_SIZE;

	inp++;
	p256_from_bin(inp, &eph_x);
	inp += P256_NBYTES;
	p256_from_bin(inp, &eph_y);
	inp += P256_NBYTES;

	/* Verify that the public point is on the curve. */
	if (!dcrypto_p256_is_valid_point(&eph_x, &eph_y))
		return 0;
	/* Compute the DH point. */
	if (!DCRYPTO_p256_point_mul(&secret_x, &secret_y,
					d, &eph_x, &eph_y))
		return 0;
	/* Check for computational errors. */
	if (!dcrypto_p256_is_valid_point(&secret_x, &secret_y))
		return 0;
	/* Convert secret to big-endian. */
	reverse(&secret_x, sizeof(secret_x));
	/* Derive shared secret. */
	if (!DCRYPTO_hkdf(key, sizeof(key), salt, salt_len,
				(uint8_t *) &secret_x, sizeof(secret_x),
				info, info_len))
		return 0;

	aes_key = &key[0];
	hmac_key = &key[AES_KEY_BYTES];
	DCRYPTO_HMAC_SHA256_init(&ctx, hmac_key, HMAC_KEY_BYTES);
	HASH_update(&ctx.hash, inp, in_len);
	if (!DCRYPTO_equals(inp + in_len, DCRYPTO_HMAC_final(&ctx),
				SHA256_DIGEST_SIZE))
		return 0;

	memmove(outp, inp, auth_data_len);
	inp += auth_data_len;
	outp += auth_data_len;
	if (!DCRYPTO_aes_ctr(outp, aes_key, AES_KEY_BYTES * 8, iv,
				inp, in_len - auth_data_len))
		return 0;
	return in_len;
}
