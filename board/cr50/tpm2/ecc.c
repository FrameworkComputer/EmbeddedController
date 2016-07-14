/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/*
 * TODO(ngm): only the NIST-P256 curve is currently supported.
 */

#include "CryptoEngine.h"
#include "TPMB.h"

#include "trng.h"
#include "util.h"
#include "dcrypto.h"

#include "cryptoc/p256.h"
#include "cryptoc/p256_ecdsa.h"

static void reverse_tpm2b(TPM2B *b)
{
	reverse(b->buffer, b->size);
}

TPM2B_BYTE_VALUE(4);
TPM2B_BYTE_VALUE(32);

static int check_p256_param(const TPM2B_ECC_PARAMETER *a)
{
	return a->b.size == sizeof(p256_int);
}

static int check_p256_point(const TPMS_ECC_POINT *a)
{
	return check_p256_param(&a->x) &&
		check_p256_param(&a->y);
}

BOOL _cpri__EccIsPointOnCurve(TPM_ECC_CURVE curve_id, TPMS_ECC_POINT *q)
{
	int result;

	switch (curve_id) {
	case TPM_ECC_NIST_P256:
		if (!check_p256_point(q))
			return FALSE;

		reverse_tpm2b(&q->x.b);
		reverse_tpm2b(&q->y.b);

		result = p256_is_valid_point((p256_int *) q->x.b.buffer,
					(p256_int *) q->y.b.buffer);

		reverse_tpm2b(&q->x.b);
		reverse_tpm2b(&q->y.b);

		if (result)
			return TRUE;
		else
			return FALSE;
	default:
		return FALSE;
	}
}

/* out = n1*G, or out = n2*in */
CRYPT_RESULT _cpri__EccPointMultiply(
	TPMS_ECC_POINT *out, TPM_ECC_CURVE curve_id,
	TPM2B_ECC_PARAMETER *n1, TPMS_ECC_POINT *in, TPM2B_ECC_PARAMETER *n2)
{
	int result;

	switch (curve_id) {
	case TPM_ECC_NIST_P256:
		if ((n1 != NULL && n2 != NULL) ||
			(n1 == NULL && n2 == NULL))
			/* Only one of n1 or n2 must be specified. */
			return CRYPT_PARAMETER;
		if ((n2 != NULL && in == NULL) ||
			(n2 == NULL && in != NULL))
			return CRYPT_PARAMETER;
		if (n1 != NULL && !check_p256_param(n1))
			return CRYPT_PARAMETER;
		if (in != NULL &&
			(!check_p256_point(in) ||
				!_cpri__EccIsPointOnCurve(curve_id, in)))
			return CRYPT_POINT;
		if (n2 != NULL && !check_p256_param(n2))
			return CRYPT_PARAMETER;

		if (n1 != NULL) {
			reverse_tpm2b(&n1->b);

			result = DCRYPTO_p256_base_point_mul(
				(p256_int *) out->x.b.buffer,
				(p256_int *) out->y.b.buffer,
				(p256_int *) n1->b.buffer);

			reverse_tpm2b(&n1->b);
		} else {
			reverse_tpm2b(&n2->b);
			reverse_tpm2b(&in->x.b);
			reverse_tpm2b(&in->y.b);

			result = DCRYPTO_p256_point_mul(
				(p256_int *) out->x.b.buffer,
				(p256_int *) out->y.b.buffer,
				(p256_int *) n2->b.buffer,
				(p256_int *) in->x.b.buffer,
				(p256_int *) in->y.b.buffer);

			reverse_tpm2b(&n2->b);
			reverse_tpm2b(&in->x.b);
			reverse_tpm2b(&in->y.b);
		}

		if (result) {
			out->x.b.size = sizeof(p256_int);
			out->y.b.size = sizeof(p256_int);
			reverse_tpm2b(&out->x.b);
			reverse_tpm2b(&out->y.b);

			return CRYPT_SUCCESS;
		} else {
			return CRYPT_NO_RESULT;
		}
	default:
		return CRYPT_PARAMETER;
	}
}

static const TPM2B_32_BYTE_VALUE ECC_TEMPLATE_EK_EXTRA = {
	.t = {32, {
			0xC2, 0xE0, 0x31, 0x93, 0x40, 0xFB, 0x48, 0xF1,
			0x02, 0x53, 0x9E, 0xA9, 0x83, 0x63, 0xF8, 0x1E,
			0x2D, 0x30, 0x6E, 0x91, 0x8D, 0xD7, 0x78, 0xAB,
			0xF0, 0x54, 0x73, 0xA2, 0xA6, 0x0D, 0xAE, 0x09,
		}
	}
};

/* Key generation based on FIPS-186.4 section B.1.2 (Key Generation by
 * Testing Candidates) */
CRYPT_RESULT _cpri__GenerateKeyEcc(
	TPMS_ECC_POINT *q, TPM2B_ECC_PARAMETER *d,
	TPM_ECC_CURVE curve_id,	TPM_ALG_ID hash_alg,
	TPM2B *seed, const char *label,	TPM2B *extra, UINT32 *counter)
{
	TPM2B_4_BYTE_VALUE marshaled_counter = { .t = {4} };
	TPM2B_32_BYTE_VALUE local_seed = { .t = {32} };
	TPM2B_4_BYTE_VALUE truncated_extra = { .t = {4} };
	TPM2B *local_extra;
	uint32_t count = 0;
	uint8_t key_bytes[P256_NBYTES];
	LITE_HMAC_CTX hmac;

	if (curve_id != TPM_ECC_NIST_P256)
		return CRYPT_PARAMETER;

	/* extra may be empty, but seed must be specified. */
	if (seed == NULL || seed->size < PRIMARY_SEED_SIZE)
		return CRYPT_PARAMETER;

	if (counter != NULL)
		count = *counter;
	if (count == 0)
		count++;

	/* Hash down the primary seed for ECC key generation, so that
	 * the derivation tree is distinct from RSA key derivation. */
	DCRYPTO_HMAC_SHA256_init(&hmac, seed->buffer, seed->size);
	HASH_update(&hmac.hash, "ECC", 4);
	memcpy(local_seed.t.buffer, DCRYPTO_HMAC_final(&hmac),
	       local_seed.t.size);
	/* TODO(ngm): CRBUG/P/55260: the personalize code uses only
	 * the first 4 bytes of extra.
	 */
	if (extra && extra->size == ECC_TEMPLATE_EK_EXTRA.b.size &&
		memcmp(extra->buffer,
		       ECC_TEMPLATE_EK_EXTRA.b.buffer,
		       ECC_TEMPLATE_EK_EXTRA.b.size) == 0) {
		memcpy(truncated_extra.b.buffer, extra->buffer, 4);
		local_extra = &truncated_extra.b;
	} else {
		local_extra = extra;
	}

	for (; count != 0; count++) {
		memcpy(marshaled_counter.t.buffer, &count, sizeof(count));
		_cpri__KDFa(hash_alg, &local_seed.b, label, local_extra,
			&marshaled_counter.b, sizeof(key_bytes) * 8, key_bytes,
			NULL, FALSE);
		if (DCRYPTO_p256_key_from_bytes(
				(p256_int *) q->x.b.buffer,
				(p256_int *) q->y.b.buffer,
				(p256_int *) d->b.buffer, key_bytes)) {
			q->x.b.size = sizeof(p256_int);
			q->y.b.size = sizeof(p256_int);
			reverse_tpm2b(&q->x.b);
			reverse_tpm2b(&q->y.b);

			d->b.size = sizeof(p256_int);
			reverse_tpm2b(&d->b);

			break;
		}
	}
	/* TODO(ngm): implement secure memset. */
	memset(local_seed.t.buffer, 0, local_seed.t.size);

	if (count == 0)
		FAIL(FATAL_ERROR_INTERNAL);
	if (counter != NULL)
		*counter = count;

	return CRYPT_SUCCESS;
}

CRYPT_RESULT _cpri__SignEcc(
	TPM2B_ECC_PARAMETER *r, TPM2B_ECC_PARAMETER *s,
	TPM_ALG_ID scheme, TPM_ALG_ID hash_alg, TPM_ECC_CURVE curve_id,
	TPM2B_ECC_PARAMETER *d, TPM2B *digest, TPM2B_ECC_PARAMETER *k)
{
	uint8_t digest_local[sizeof(p256_int)];
	const size_t digest_len = MIN(digest->size, sizeof(digest_local));
	p256_int p256_digest;

	if (curve_id != TPM_ECC_NIST_P256)
		return CRYPT_PARAMETER;

	switch (scheme) {
	case TPM_ALG_ECDSA:
		if (!check_p256_param(d))
			return CRYPT_PARAMETER;
		/* Trucate / zero-pad the digest as appropriate. */
		memset(digest_local, 0, sizeof(digest_local));
		memcpy(digest_local + sizeof(digest_local) - digest_len,
			digest->buffer, digest_len);
		p256_from_bin(digest_local, &p256_digest);

		reverse_tpm2b(&d->b);

		p256_ecdsa_sign((p256_int *) d->b.buffer,
				&p256_digest,
				(p256_int *) r->b.buffer,
				(p256_int *) s->b.buffer);
		reverse_tpm2b(&d->b);

		r->b.size = sizeof(p256_int);
		s->b.size = sizeof(p256_int);
		reverse_tpm2b(&r->b);
		reverse_tpm2b(&s->b);

		return CRYPT_SUCCESS;
	default:
		return CRYPT_PARAMETER;
	}
}

CRYPT_RESULT _cpri__ValidateSignatureEcc(
	TPM2B_ECC_PARAMETER *r,	TPM2B_ECC_PARAMETER *s,
	TPM_ALG_ID scheme, TPM_ALG_ID hash_alg,
	TPM_ECC_CURVE curve_id,	TPMS_ECC_POINT *q, TPM2B *digest)
{
	uint8_t digest_local[sizeof(p256_int)];
	const size_t digest_len = MIN(digest->size, sizeof(digest_local));
	p256_int p256_digest;
	int result;

	if (curve_id != TPM_ECC_NIST_P256)
		return CRYPT_PARAMETER;

	switch (scheme) {
	case TPM_ALG_ECDSA:
		/* Trucate / zero-pad the digest as appropriate. */
		memset(digest_local, 0, sizeof(digest_local));
		memcpy(digest_local + sizeof(digest_local) - digest_len,
			digest->buffer, digest_len);
		p256_from_bin(digest_local, &p256_digest);

		reverse_tpm2b(&q->x.b);
		reverse_tpm2b(&q->y.b);

		reverse_tpm2b(&r->b);
		reverse_tpm2b(&s->b);

		result = p256_ecdsa_verify(
			(p256_int *) q->x.b.buffer,
			(p256_int *) q->y.b.buffer,
			&p256_digest,
			(p256_int *) r->b.buffer,
			(p256_int *) s->b.buffer);

		reverse_tpm2b(&q->x.b);
		reverse_tpm2b(&q->y.b);

		reverse_tpm2b(&r->b);
		reverse_tpm2b(&s->b);

		if (result)
			return CRYPT_SUCCESS;
		else
			return CRYPT_FAIL;
	default:
		return CRYPT_PARAMETER;
	}
}

CRYPT_RESULT _cpri__GetEphemeralEcc(TPMS_ECC_POINT *q, TPM2B_ECC_PARAMETER *d,
				TPM_ECC_CURVE curve_id)
{
	uint8_t key_bytes[P256_NBYTES] __aligned(4);

	if (curve_id != TPM_ECC_NIST_P256)
		return CRYPT_PARAMETER;

	rand_bytes(key_bytes, sizeof(key_bytes));

	if (DCRYPTO_p256_key_from_bytes((p256_int *) q->x.b.buffer,
						(p256_int *) q->y.b.buffer,
						(p256_int *) d->b.buffer,
						key_bytes)) {
		q->x.b.size = sizeof(p256_int);
		q->y.b.size = sizeof(p256_int);
		reverse_tpm2b(&q->x.b);
		reverse_tpm2b(&q->y.b);

		d->b.size = sizeof(p256_int);
		reverse_tpm2b(&d->b);

		return CRYPT_SUCCESS;
	} else {
		return CRYPT_FAIL;
	}
}

#ifdef CRYPTO_TEST_SETUP

#include "extension.h"

enum {
	TEST_SIGN = 0,
	TEST_VERIFY = 1,
	TEST_KEYGEN = 2,
	TEST_KEYDERIVE = 3
};

struct TPM2B_ECC_PARAMETER_aligned {
	uint16_t pad;
	TPM2B_ECC_PARAMETER d;
} __packed __aligned(4);

struct TPM2B_MAX_BUFFER_aligned {
	uint16_t pad;
	TPM2B_MAX_BUFFER d;
} __packed __aligned(4);

static const struct TPM2B_ECC_PARAMETER_aligned NIST_P256_d = {
	.d = {
		.t = {32, {
				0xfc, 0x44, 0x1e, 0x07, 0x74, 0x4e, 0x48, 0xf1,
				0x09, 0xb7, 0xe6, 0x6b, 0x29, 0x48, 0x2f, 0x7b,
				0x7e, 0x3e, 0xc9, 0x1f, 0xa2, 0x7f, 0xd4, 0x87,
				0x09, 0x91, 0xb2, 0x89, 0xfe, 0xa0, 0xd2, 0x0a
			}
		}
	}
};

static const struct TPM2B_ECC_PARAMETER_aligned NIST_P256_qx = {
	.d = {
		.t = {32, {
				0x12, 0xc3, 0xd6, 0xa2, 0x67, 0x9c, 0xa8, 0xee,
				0x3c, 0x4d, 0x92, 0x7f, 0x20, 0x4e, 0xd5, 0xbc,
				0xb4, 0x57, 0x7a, 0x04, 0xb0, 0xac, 0x02, 0xb2,
				0xa3, 0x6a, 0xb3, 0xe9, 0xe1, 0x07, 0x81, 0xde
			}
		}
	}
};

static const struct TPM2B_ECC_PARAMETER_aligned NIST_P256_qy = {
	.d = {
		.t = {32, {
				0x5c, 0x85, 0xad, 0x74, 0x13, 0x97, 0x11, 0x72,
				0xfc, 0xa5, 0x73, 0x8f, 0xee, 0x9d, 0x0e, 0x7b,
				0xc5, 0x9f, 0xfd, 0x8a, 0x62, 0x6d, 0x68, 0x9b,
				0xc6, 0xcc, 0xa4, 0xb5, 0x86, 0x65, 0x52, 0x1d
			}
		}
	}
};

#define MAX_MSG_BYTES MAX_DIGEST_BUFFER

static int point_equals(const TPMS_ECC_POINT *a, const TPMS_ECC_POINT *b)
{
	return a->x.b.size == b->x.b.size &&
		a->y.b.size == b->y.b.size &&
		memcmp(a->x.b.buffer, b->x.b.buffer, a->x.b.size) == 0 &&
		memcmp(a->y.b.buffer, b->y.b.buffer, a->y.b.size) == 0;

}

static void ecc_command_handler(void *cmd_body, size_t cmd_size,
			size_t *response_size_out)
{
	uint8_t *cmd;
	uint8_t op;
	uint8_t curve_id;
	uint8_t sign_mode;
	uint8_t hashing;
	uint16_t in_len;
	uint8_t in[MAX_MSG_BYTES];
	uint16_t digest_len;
	struct TPM2B_MAX_BUFFER_aligned digest;
	uint8_t *out = (uint8_t *) cmd_body;
	uint32_t *response_size = (uint32_t *) response_size_out;

	TPMS_ECC_POINT q;
	TPM2B_ECC_PARAMETER d;
	struct TPM2B_ECC_PARAMETER_aligned r;
	struct TPM2B_ECC_PARAMETER_aligned s;

	/* Command format.
	 *
	 *   OFFSET       FIELD
	 *   0            OP
	 *   1            CURVE_ID
	 *   2            SIGN_MODE
	 *   3            HASHING
	 *   4            MSB IN LEN
	 *   5            LSB IN LEN
	 *   6            IN
	 *   6 + IN_LEN   MSB DIGEST LEN
	 *   7 + IN_LEN   LSB DIGEST LEN
	 *   8 + IN_LEN   DIGEST
	 */

	cmd = (uint8_t *) cmd_body;
	op = *cmd++;
	curve_id = *cmd++;
	sign_mode = *cmd++;
	hashing = *cmd++;
	in_len = ((uint16_t) (cmd[0] << 8)) | cmd[1];
	cmd += 2;
	if (in_len > sizeof(in)) {
		*response_size = 0;
		return;
	}
	memcpy(in, cmd, in_len);
	cmd += in_len;

	digest_len = ((uint16_t) (cmd[0] << 8)) | cmd[1];
	cmd += 2;
	if (digest_len > sizeof(digest.d.t.buffer)) {
		*response_size = 0;
		return;
	}
	digest.d.t.size = digest_len;
	memcpy(digest.d.t.buffer, cmd, digest_len);
	cmd += digest_len;

	/* Make copies of d, and q, as const data is immutable. */
	switch (curve_id) {
	case TPM_ECC_NIST_P256:
		d = NIST_P256_d.d;
		q.x = NIST_P256_qx.d;
		q.y = NIST_P256_qy.d;
		break;
	default:
		*response_size = 0;
		return;
	}

	switch (op) {
	case TEST_SIGN:
		if (_cpri__SignEcc(&r.d, &s.d, sign_mode, hashing,
					curve_id, &d, &digest.d.b, NULL)
			!= CRYPT_SUCCESS) {
			*response_size = 0;
			return;
		}
		memcpy(out, r.d.b.buffer, r.d.b.size);
		out += r.d.b.size;
		memcpy(out, s.d.b.buffer, s.d.b.size);
		*response_size = r.d.b.size + s.d.b.size;
		break;
	case TEST_VERIFY:
		r.d.b.size = in_len / 2;
		memcpy(r.d.b.buffer, in, r.d.b.size);
		s.d.b.size = in_len / 2;
		memcpy(s.d.b.buffer, in + r.d.b.size, s.d.b.size);
		if (_cpri__ValidateSignatureEcc(
				&r.d, &s.d, sign_mode, hashing, curve_id,
				&q, &digest.d.b) != CRYPT_SUCCESS) {
			*response_size = 0;
		} else {
			*out = 1;
			*response_size = 1;
		}
		return;
	case TEST_KEYGEN:
	{
		struct TPM2B_ECC_PARAMETER_aligned d_local;
		TPMS_ECC_POINT q_local;

		if (_cpri__GetEphemeralEcc(&q, &d_local.d, curve_id)
			!= CRYPT_SUCCESS) {
			*response_size = 0;
			return;
		}

		if (_cpri__EccIsPointOnCurve(curve_id, &q) != TRUE) {
			*response_size = 0;
			return;
		}

		/* Verify correspondence of secret with the public point. */
		if (_cpri__EccPointMultiply(
				&q_local, curve_id, &d_local.d,
				NULL, NULL) != CRYPT_SUCCESS) {
			*response_size = 0;
			return;
		}
		if (!point_equals(&q, &q_local)) {
			*response_size = 0;
			return;
		}
		*out = 1;
		*response_size = 1;
		return;
	}
	case TEST_KEYDERIVE:
	{
		/* Random seed. */
		TPM2B_SEED seed;
		struct TPM2B_ECC_PARAMETER_aligned d_local;
		TPMS_ECC_POINT q_local;
		const char *label = "ecc_test";


		if (in_len > PRIMARY_SEED_SIZE) {
			*response_size = 0;
			return;
		}
		seed.t.size = in_len;
		memcpy(seed.t.buffer, in, in_len);

		if (_cpri__GenerateKeyEcc(
				&q, &d_local.d, curve_id, hashing,
				&seed.b, label, NULL, NULL) != CRYPT_SUCCESS) {
			*response_size = 0;
			return;
		}

		if (_cpri__EccIsPointOnCurve(curve_id, &q) != TRUE) {
			*response_size = 0;
			return;
		}

		/* Verify correspondence of secret with the public point. */
		if (_cpri__EccPointMultiply(
				&q_local, curve_id, &d_local.d,
				NULL, NULL) != CRYPT_SUCCESS) {
			*response_size = 0;
			return;
		}
		if (!point_equals(&q, &q_local)) {
			*response_size = 0;
			return;
		}

		*out = 1;
		*response_size = 1;
		return;
	}
	default:
		*response_size = 0;
		return;
	}
}

DECLARE_EXTENSION_COMMAND(EXTENSION_EC, ecc_command_handler);

#endif   /* CRYPTO_TEST_SETUP */
