/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "dcrypto.h"
#include "internal.h"

#include "trng.h"
#include "util.h"

#include <assert.h>

#include "cryptoc/sha.h"
#include "cryptoc/sha256.h"

static void MGF1_xor(uint8_t *dst, uint32_t dst_len,
		const uint8_t *seed, uint32_t seed_len,
		enum hashing_mode hashing)
{
	HASH_CTX ctx;
	struct {
		uint8_t b3;
		uint8_t b2;
		uint8_t b1;
		uint8_t b0;
	} cnt;
	const uint8_t *digest;
	const size_t hash_size = (hashing == HASH_SHA1) ? SHA_DIGEST_SIZE
		: SHA256_DIGEST_SIZE;

	cnt.b0 = cnt.b1 = cnt.b2 = cnt.b3 = 0;
	while (dst_len) {
		int i;

		if (hashing == HASH_SHA1)
			DCRYPTO_SHA1_init(&ctx, 0);
		else
			DCRYPTO_SHA256_init(&ctx, 0);

		HASH_update(&ctx, seed, seed_len);
		HASH_update(&ctx, (uint8_t *) &cnt, sizeof(cnt));
		digest = HASH_final(&ctx);
		for (i = 0; i < dst_len && i < hash_size; ++i)
			*dst++ ^= *digest++;
		dst_len -= i;
		if (!++cnt.b0)
			++cnt.b1;
	}
}

/*
 * struct OAEP {                  // MSB to LSB.
 *      uint8_t zero;
 *      uint8_t seed[HASH_SIZE];
 *      uint8_t phash[HASH_SIZE];
 *      uint8_t PS[];             // Variable length (optional) zero-pad.
 *      uint8_t one;              // 0x01, message demarcator.
 *      uint8_t msg[];            // Input message.
 * };
 */
/* encrypt */
static int oaep_pad(uint8_t *output, uint32_t output_len,
		const uint8_t *msg, uint32_t msg_len,
		enum hashing_mode hashing, const char *label)
{
	int i;
	const size_t hash_size = (hashing == HASH_SHA1) ? SHA_DIGEST_SIZE
		: SHA256_DIGEST_SIZE;
	uint8_t *const seed = output + 1;
	uint8_t *const phash = seed + hash_size;
	uint8_t *const PS = phash + hash_size;
	const uint32_t max_msg_len = output_len - 2 - 2 * hash_size;
	const uint32_t ps_len = max_msg_len - msg_len;
	uint8_t *const one = PS + ps_len;
	struct HASH_CTX ctx;

	if (output_len < 2 + 2 * hash_size)
		return 0;       /* Key size too small for chosen hash. */
	if (msg_len > output_len - 2 - 2 * hash_size)
		return 0;       /* Input message too large for key size. */

	dcrypto_memset(output, 0, output_len);
	for (i = 0; i < hash_size;) {
		uint32_t r = rand();

		seed[i++] = r >> 0;
		seed[i++] = r >> 8;
		seed[i++] = r >> 16;
		seed[i++] = r >> 24;
	}

	if (hashing == HASH_SHA1)
		DCRYPTO_SHA1_init(&ctx, 0);
	else
		DCRYPTO_SHA256_init(&ctx, 0);

	HASH_update(&ctx, label, label ? strlen(label) + 1 : 0);
	memcpy(phash, HASH_final(&ctx), hash_size);
	*one = 1;
	memcpy(one + 1, msg, msg_len);
	MGF1_xor(phash, hash_size + 1 + max_msg_len,
		seed, hash_size, hashing);
	MGF1_xor(seed, hash_size, phash, hash_size + 1 + max_msg_len,
		hashing);
	return 1;
}

/* decrypt */
/* TODO(ngm): constant time. */
static int check_oaep_pad(uint8_t *out, uint32_t *out_len,
			uint8_t *padded, uint32_t padded_len,
			enum hashing_mode hashing, const char *label)
{
	const size_t hash_size = (hashing == HASH_SHA1) ? SHA_DIGEST_SIZE
		: SHA256_DIGEST_SIZE;
	uint8_t *seed = padded + 1;
	uint8_t *phash = seed + hash_size;
	uint8_t *PS = phash + hash_size;
	const uint32_t max_msg_len = padded_len - 2 - 2 * hash_size;
	struct HASH_CTX ctx;
	int one_index = -1;
	int bad;
	int i;

	if (padded_len < 2 + 2 * hash_size)
		return 0;       /* Invalid input size. */

	/* Recover seed. */
	MGF1_xor(seed, hash_size, phash, hash_size + 1 + max_msg_len, hashing);
	/* Recover db. */
	MGF1_xor(phash, hash_size + 1 + max_msg_len, seed, hash_size, hashing);

	if (hashing == HASH_SHA1)
		DCRYPTO_SHA1_init(&ctx, 0);
	else
		DCRYPTO_SHA256_init(&ctx, 0);
	HASH_update(&ctx, label, label ? strlen(label) + 1 : 0);

	bad = memcmp(phash, HASH_final(&ctx), hash_size);
	bad |= padded[0];

	for (i = PS - padded; i <  padded_len; i++) {
		if (padded[i] == 1) {
			one_index = i;
			break;
		} else if (padded[i] != 0) {
			bad = 1;
			break;
		}
	}

	if (one_index < 0 || bad)
		return 0;
	one_index++;
	if (*out_len < padded_len - one_index)
		return 0;
	memcpy(out, padded + one_index, padded_len - one_index);
	*out_len = padded_len - one_index;
	return 1;
}

/* Constants from RFC 3447. */
#define RSA_PKCS1_PADDING_SIZE 11

/* encrypt */
static int pkcs1_type2_pad(uint8_t *padded, uint32_t padded_len,
		const uint8_t *in, uint32_t in_len)
{
	uint32_t PS_len;

	if (padded_len < RSA_PKCS1_PADDING_SIZE)
		return 0;
	if (in_len > padded_len - RSA_PKCS1_PADDING_SIZE)
		return 0;
	PS_len = padded_len - 3 - in_len;

	*(padded++) = 0;
	*(padded++) = 2;
	while (PS_len) {
		int i;
		uint32_t r = rand();

		for (i = 0; i < 4 && PS_len; i++) {
			uint8_t b = ((uint8_t *) &r)[i];

			if (b) {
				*padded++ = b;
				PS_len--;
			}
		}
	}
	*(padded++) = 0;
	memcpy(padded, in, in_len);
	return 1;
}

/* decrypt */
/* TODO(ngm): constant time */
static int check_pkcs1_type2_pad(uint8_t *out, uint32_t *out_len,
				const uint8_t *padded, uint32_t padded_len)
{
	int i;

	if (padded_len < RSA_PKCS1_PADDING_SIZE)
		return 0;
	if (padded[0] != 0 || padded[1] != 2)
		return 0;
	for (i = 2; i < padded_len; i++) {
		if (padded[i] == 0)
			break;
	}

	if (i == padded_len)
		return 0;
	i++;
	if (i < RSA_PKCS1_PADDING_SIZE)
		return 0;
	if (*out_len < padded_len - i)
		return 0;
	memcpy(out, &padded[i], padded_len - i);
	*out_len = padded_len - i;
	return 1;
}

static const uint8_t SHA1_DER[] = {
	0x30, 0x21, 0x30, 0x09, 0x06, 0x05, 0x2b, 0x0e,
	0x03, 0x02, 0x1a, 0x05, 0x00, 0x04, 0x14
};
static const uint8_t SHA256_DER[] = {
	0x30, 0x31, 0x30, 0x0D, 0x06, 0x09, 0x60, 0x86,
	0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05,
	0x00, 0x04, 0x20
};

/* sign */
static int pkcs1_type1_pad(uint8_t *padded, uint32_t padded_len,
			const uint8_t *in, uint32_t in_len,
			enum hashing_mode hashing)
{
	const uint8_t *der = (hashing == HASH_SHA1) ? &SHA1_DER[0]
		: &SHA256_DER[0];
	const uint32_t der_size = (hashing == HASH_SHA1) ? sizeof(SHA1_DER)
		: sizeof(SHA256_DER);
	const uint32_t hash_size = (hashing == HASH_SHA1) ? SHA_DIGEST_SIZE
		: SHA256_DIGEST_SIZE;
	uint32_t ps_len;

	if (padded_len < RSA_PKCS1_PADDING_SIZE + der_size)
		return 0;
	if (in_len != hash_size)
		return 0;
	if (in_len > padded_len - RSA_PKCS1_PADDING_SIZE - der_size)
		return 0;
	ps_len = padded_len - 3 - der_size - in_len;

	*(padded++) = 0;
	*(padded++) = 1;
	dcrypto_memset(padded, 0xFF, ps_len);
	padded += ps_len;
	*(padded++) = 0;
	memcpy(padded, der, der_size);
	padded += der_size;
	memcpy(padded, in, in_len);
	return 1;
}

/* verify */
/* TODO(ngm): constant time */
static int check_pkcs1_type1_pad(const uint8_t *msg, uint32_t msg_len,
				const uint8_t *padded, uint32_t padded_len,
				enum hashing_mode hashing)
{
	int i;
	const uint8_t *der = (hashing == HASH_SHA1) ? &SHA1_DER[0]
		: &SHA256_DER[0];
	const uint32_t der_size = (hashing == HASH_SHA1) ? sizeof(SHA1_DER)
		: sizeof(SHA256_DER);
	const uint32_t hash_size = (hashing == HASH_SHA1) ? SHA_DIGEST_SIZE
		: SHA256_DIGEST_SIZE;
	uint32_t ps_len;

	if (msg_len != hash_size)
		return 0;
	if (padded_len < RSA_PKCS1_PADDING_SIZE + der_size + hash_size)
		return 0;
	ps_len = padded_len - 3 - der_size - hash_size;

	if (padded[0] != 0 || padded[1] != 1)
		return 0;
	for (i = 2; i < ps_len + 2; i++) {
		if (padded[i] != 0xFF)
			return 0;
	}

	if (padded[i++] != 0)
		return 0;
	if (memcmp(&padded[i], der, der_size) != 0)
		return 0;
	i += der_size;
	return memcmp(msg, &padded[i], hash_size) == 0;
}

/* sign */
static int pkcs1_pss_pad(uint8_t *padded, uint32_t padded_len,
			const uint8_t *in, uint32_t in_len,
			enum hashing_mode hashing)
{
	const uint32_t hash_size = (hashing == HASH_SHA1) ? SHA_DIGEST_SIZE
		: SHA256_DIGEST_SIZE;
	const uint32_t salt_len = MIN(padded_len - hash_size - 2, hash_size);
	uint32_t db_len;
	uint32_t ps_len;
	struct HASH_CTX ctx;

	if (in_len != hash_size)
		return 0;
	if (padded_len < hash_size + 2)
		return 0;
	db_len = padded_len - hash_size - 1;

	if (hashing == HASH_SHA1)
		DCRYPTO_SHA1_init(&ctx, 0);
	else
		DCRYPTO_SHA256_init(&ctx, 0);

	/* Pilfer bits of output for temporary use. */
	memset(padded, 0, 8);
	HASH_update(&ctx, padded, 8);
	HASH_update(&ctx, in, in_len);
	/* Pilfer bits of output for temporary use. */
	rand_bytes(padded, salt_len);
	HASH_update(&ctx, padded, salt_len);

	/* Output hash. */
	memcpy(padded + db_len, HASH_final(&ctx), hash_size);

	/* Prepare DB. */
	ps_len = db_len - salt_len - 1;
	memmove(padded + ps_len + 1, padded, salt_len);
	memset(padded, 0, ps_len);
	padded[ps_len] = 0x01;
	MGF1_xor(padded, db_len, padded + db_len, hash_size, hashing);

	/* Clear most significant bit. */
	padded[0] &= 0x7F;
	/* Set trailing byte. */
	padded[padded_len - 1] = 0xBC;
	return 1;
}

/* verify */
static int check_pkcs1_pss_pad(const uint8_t *in, uint32_t in_len,
			uint8_t *padded, uint32_t padded_len,
			enum hashing_mode hashing)
{
	const uint32_t hash_size = (hashing == HASH_SHA1) ? SHA_DIGEST_SIZE
		: SHA256_DIGEST_SIZE;
	const uint8_t zeros[8] = {0, 0, 0, 0, 0, 0, 0, 0};
	uint32_t db_len;
	uint32_t max_ps_len;
	uint32_t salt_len;
	HASH_CTX ctx;
	int bad = 0;
	int i;

	if (in_len != hash_size)
		return 0;
	if (padded_len < hash_size + 2)
		return 0;
	db_len = padded_len - hash_size - 1;

	/* Top bit should be zero. */
	bad |= padded[0] & 0x80;
	/* Check trailing byte. */
	bad |= padded[padded_len - 1] ^ 0xBC;

	/* Recover DB. */
	MGF1_xor(padded, db_len, padded + db_len, hash_size, hashing);
	/* Clear top bit. */
	padded[0] &= 0x7F;
	/* Verify padding2. */
	max_ps_len = db_len - 1;
	for (i = 0; i < max_ps_len; i++) {
		if (padded[i] == 0x01)
			break;
		else
			bad |= padded[i];
	}
	bad |= (padded[i] ^ 0x01);
	/* Continue with zero-length salt if 0x01 was not found. */
	salt_len = max_ps_len - i;

	if (hashing == HASH_SHA1)
		DCRYPTO_SHA1_init(&ctx, 0);
	else
		DCRYPTO_SHA256_init(&ctx, 0);
	HASH_update(&ctx, zeros, sizeof(zeros));
	HASH_update(&ctx, in, in_len);
	HASH_update(&ctx, padded + db_len - salt_len, salt_len);
	bad |= memcmp(padded + db_len, HASH_final(&ctx), hash_size);
	return !bad;
}

static int check_modulus_params(const struct LITE_BIGNUM *N, uint32_t *out_len)
{
	if (bn_size(N) > RSA_MAX_BYTES)
		return 0;                      /* Unsupported key size. */
	if (!bn_check_topbit(N))               /* Check that top bit is set. */
		return 0;
	if (out_len && *out_len < bn_size(N))
		return 0;                      /* Output buffer too small. */
	return 1;
}

int DCRYPTO_rsa_encrypt(struct RSA *rsa, uint8_t *out, uint32_t *out_len,
			const uint8_t *in, uint32_t in_len,
			enum padding_mode padding, enum hashing_mode hashing,
			const char *label)
{
	uint8_t *p;
	uint32_t padded_buf[RSA_MAX_WORDS];
	uint32_t e_buf[LITE_BN_BYTES / sizeof(uint32_t)];

	struct LITE_BIGNUM padded;
	struct LITE_BIGNUM e;
	struct LITE_BIGNUM encrypted;

	if (!check_modulus_params(&rsa->N, out_len))
		return 0;

	bn_init(&padded, padded_buf, bn_size(&rsa->N));
	bn_init(&encrypted, out, bn_size(&rsa->N));
	bn_init(&e, e_buf, sizeof(e_buf));
	BN_DIGIT(&e, 0) = rsa->e;

	switch (padding) {
	case PADDING_MODE_OAEP:
		if (!oaep_pad((uint8_t *) padded.d, bn_size(&padded),
				(const uint8_t *) in, in_len, hashing, label))
			return 0;
		break;
	case PADDING_MODE_PKCS1:
		if (!pkcs1_type2_pad((uint8_t *) padded.d, bn_size(&padded),
					(const uint8_t *) in, in_len))
			return 0;
		break;
	case PADDING_MODE_NULL:
		/* Input is allowed to have more bytes than N, in
		 * which case the excess must be zero. */
		for (; in_len > bn_size(&padded); in_len--)
			if (*in++ != 0)
				return 0;
		p = (uint8_t *) padded.d;
		/* If in_len < bn_size(&padded), padded will
		 * have leading zero bytes. */
		memcpy(&p[bn_size(&padded) - in_len], in, in_len);
		/* TODO(ngm): in may be > N, bn_mont_mod_exp() should
		 * handle this case. */
		break;
	default:
		return 0;                       /* Unsupported padding mode. */
	}

	/* Reverse from big-endian to little-endian notation. */
	reverse((uint8_t *) padded.d, bn_size(&padded));
	bn_mont_modexp(&encrypted, &padded, &e, &rsa->N);
	/* Back to big-endian notation. */
	reverse((uint8_t *) encrypted.d, bn_size(&encrypted));
	*out_len = bn_size(&encrypted);

	dcrypto_memset(padded_buf, 0, sizeof(padded_buf));
	dcrypto_memset(e_buf, 0, sizeof(e_buf));
	return 1;
}

int DCRYPTO_rsa_decrypt(struct RSA *rsa, uint8_t *out, uint32_t *out_len,
			const uint8_t *in, const uint32_t in_len,
			enum padding_mode padding, enum hashing_mode hashing,
			const char *label)
{
	uint32_t encrypted_buf[RSA_MAX_WORDS];
	uint32_t padded_buf[RSA_MAX_WORDS];

	struct LITE_BIGNUM encrypted;
	struct LITE_BIGNUM padded;
	int ret = 1;

	if (!check_modulus_params(&rsa->N, NULL))
		return 0;
	if (in_len != bn_size(&rsa->N))
		return 0;                      /* Invalid input length. */

	/* TODO(ngm): this copy can be eliminated if input may be modified. */
	bn_init(&encrypted, encrypted_buf, in_len);
	memcpy(encrypted_buf, in, in_len);
	bn_init(&padded, padded_buf, in_len);

	/* Reverse from big-endian to little-endian notation. */
	reverse((uint8_t *) encrypted.d, encrypted.dmax * LITE_BN_BYTES);
	bn_mont_modexp(&padded, &encrypted, &rsa->d, &rsa->N);
	/* Back to big-endian notation. */
	reverse((uint8_t *) padded.d, padded.dmax * LITE_BN_BYTES);

	switch (padding) {
	case PADDING_MODE_OAEP:
		if (!check_oaep_pad(out, out_len, (uint8_t *) padded.d,
					bn_size(&padded), hashing, label))
			ret = 0;
		break;
	case PADDING_MODE_PKCS1:
		if (!check_pkcs1_type2_pad(
				out, out_len, (const uint8_t *) padded.d,
				bn_size(&padded)))
			ret = 0;
		break;
	case PADDING_MODE_NULL:
		if (*out_len < bn_size(&padded)) {
			ret = 0;
		} else {
			*out_len = bn_size(&padded);
			memcpy(out, padded.d, *out_len);
		}
		break;
	default:
		/* Unsupported padding mode. */
		ret = 0;
		break;
	}

	dcrypto_memset(encrypted_buf, 0, sizeof(encrypted_buf));
	dcrypto_memset(padded_buf, 0, sizeof(padded_buf));
	return ret;
}

int DCRYPTO_rsa_sign(struct RSA *rsa, uint8_t *out, uint32_t *out_len,
		const uint8_t *in, const uint32_t in_len,
		enum padding_mode padding, enum hashing_mode hashing)
{
	uint32_t padded_buf[RSA_MAX_WORDS];

	struct LITE_BIGNUM padded;
	struct LITE_BIGNUM signature;

	if (!check_modulus_params(&rsa->N, out_len))
		return 0;

	bn_init(&padded, padded_buf, bn_size(&rsa->N));
	bn_init(&signature, out, bn_size(&rsa->N));

	switch (padding) {
	case PADDING_MODE_PKCS1:
		if (!pkcs1_type1_pad((uint8_t *) padded.d, bn_size(&padded),
					(const uint8_t *) in, in_len, hashing))
			return 0;
		break;
	case PADDING_MODE_PSS:
		if (!pkcs1_pss_pad((uint8_t *) padded.d, bn_size(&padded),
					(const uint8_t *) in, in_len, hashing))
			return 0;
		break;
	default:
		return 0;
	}

	/* Reverse from big-endian to little-endian notation. */
	reverse((uint8_t *) padded.d, bn_size(&padded));
	bn_mont_modexp(&signature, &padded, &rsa->d, &rsa->N);
	/* Back to big-endian notation. */
	reverse((uint8_t *) signature.d, bn_size(&signature));
	*out_len = bn_size(&rsa->N);

	dcrypto_memset(padded_buf, 0, sizeof(padded_buf));
	return 1;
}

int DCRYPTO_rsa_verify(const struct RSA *rsa, const uint8_t *digest,
		uint32_t digest_len, const uint8_t *sig,
		const uint32_t sig_len,	enum padding_mode padding,
		enum hashing_mode hashing)
{
	uint32_t padded_buf[RSA_MAX_WORDS];
	uint32_t signature_buf[RSA_MAX_WORDS];
	uint32_t e_buf[LITE_BN_BYTES / sizeof(uint32_t)];

	struct LITE_BIGNUM padded;
	struct LITE_BIGNUM signature;
	struct LITE_BIGNUM e;
	int ret = 1;

	if (!check_modulus_params(&rsa->N, NULL))
		return 0;
	if (sig_len != bn_size(&rsa->N))
		return 0;                      /* Invalid input length. */

	bn_init(&signature, signature_buf, bn_size(&rsa->N));
	memcpy(signature_buf, sig, bn_size(&rsa->N));
	bn_init(&padded, padded_buf, bn_size(&rsa->N));
	bn_init(&e, e_buf, sizeof(e_buf));
	BN_DIGIT(&e, 0) = rsa->e;

	/* Reverse from big-endian to little-endian notation. */
	reverse((uint8_t *) signature.d, signature.dmax * LITE_BN_BYTES);
	bn_mont_modexp(&padded, &signature, &e, &rsa->N);
	/* Back to big-endian notation. */
	reverse((uint8_t *) padded.d, padded.dmax * LITE_BN_BYTES);

	switch (padding) {
	case PADDING_MODE_PKCS1:
		if (!check_pkcs1_type1_pad(
				digest, digest_len, (uint8_t *) padded.d,
				bn_size(&padded), hashing))
			ret = 0;
		break;
	case PADDING_MODE_PSS:
		if (!check_pkcs1_pss_pad(
				digest, digest_len, (uint8_t *) padded.d,
				bn_size(&padded), hashing))
			return 0;
		break;
	default:
		/* Unsupported padding mode. */
		ret = 0;
		break;
	}

	dcrypto_memset(padded_buf, 0, sizeof(padded_buf));
	dcrypto_memset(signature_buf, 0, sizeof(signature_buf));
	return ret;
}

int DCRYPTO_rsa_key_compute(struct LITE_BIGNUM *N, struct LITE_BIGNUM *d,
			struct LITE_BIGNUM *p, struct LITE_BIGNUM *q,
			uint32_t e_buf)
{
	uint32_t ONE_buf = 1;
	uint32_t phi_buf[RSA_MAX_WORDS];
	uint32_t q_buf[RSA_MAX_WORDS / 2];

	struct LITE_BIGNUM ONE;
	struct LITE_BIGNUM e;
	struct LITE_BIGNUM phi;
	struct LITE_BIGNUM q_local;

	DCRYPTO_bn_wrap(&ONE, &ONE_buf, sizeof(ONE_buf));
	DCRYPTO_bn_wrap(&phi, phi_buf, bn_size(N));
	if (!q) {
		/* q not provided, calculate it. */
		memcpy(phi_buf, N->d, bn_size(N));
		bn_init(&q_local, q_buf, bn_size(p));
		bn_sub(&phi, &ONE);
		if (!bn_modinv_vartime(&q_local, p, &phi))
			return 0;
		q = &q_local;
		bn_add(&phi, &ONE);
	} else {
		DCRYPTO_bn_mul(N, p, q);
		memcpy(phi_buf, N->d, bn_size(N));
	}

	bn_sub(&phi, p);
	bn_sub(&phi, q);
	bn_add(&phi, &ONE);
	DCRYPTO_bn_wrap(&e, &e_buf, sizeof(e_buf));
	return bn_modinv_vartime(d, &e, &phi);
}
