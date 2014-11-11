/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Implementation of RSA signature verification which uses a pre-processed key
 * for computation.
 */

#include "rsa.h"
#include "sha256.h"
#include "util.h"

/**
 * a[] -= mod
 */
static void sub_mod(const struct rsa_public_key *key, uint32_t *a)
{
	int64_t A = 0;
	uint32_t i;
	for (i = 0; i < RSANUMWORDS; ++i) {
		A += (uint64_t)a[i] - key->n[i];
		a[i] = (uint32_t)A;
		A >>= 32;
	}
}

/**
 * Return a[] >= mod
 */
static int ge_mod(const struct rsa_public_key *key, const uint32_t *a)
{
	uint32_t i;
	for (i = RSANUMWORDS; i;) {
		--i;
		if (a[i] < key->n[i])
			return 0;
		if (a[i] > key->n[i])
			return 1;
	}
	return 1;  /* equal */
}

/**
 * Montgomery c[] += a * b[] / R % mod
 */
static void mont_mul_add(const struct rsa_public_key *key,
			 uint32_t *c,
			 const uint32_t a,
			 const uint32_t *b)
{
	uint64_t A = (uint64_t)a * b[0] + c[0];
	uint32_t d0 = (uint32_t)A * key->n0inv;
	uint64_t B = (uint64_t)d0 * key->n[0] + (uint32_t)A;
	uint32_t i;

	for (i = 1; i < RSANUMWORDS; ++i) {
		A = (A >> 32) + (uint64_t)a * b[i] + c[i];
		B = (B >> 32) + (uint64_t)d0 * key->n[i] + (uint32_t)A;
		c[i - 1] = (uint32_t)B;
	}

	A = (A >> 32) + (B >> 32);

	c[i - 1] = (uint32_t)A;

	if (A >> 32)
		sub_mod(key, c);
}

/**
 * Montgomery c[] = a[] * b[] / R % mod
 */
static void mont_mul(const struct rsa_public_key *key,
		     uint32_t *c,
		     const uint32_t *a,
		     const uint32_t *b)
{
	uint32_t i;
	for (i = 0; i < RSANUMWORDS; ++i)
		c[i] = 0;

	for (i = 0; i < RSANUMWORDS; ++i)
		mont_mul_add(key, c, a[i], b);
}

/**
 * In-place public exponentiation.
 *
 * @param key		Key to use in signing
 * @param inout		Input and output big-endian byte array
 * @param workbuf32	Work buffer; caller must verify this is
 *			3 x RSANUMWORDS elements long.
 */
static void mod_pow_F4(const struct rsa_public_key *key, uint8_t *inout,
		    uint32_t *workbuf32)
{
	uint32_t *a = workbuf32;
	uint32_t *a_r = a + RSANUMWORDS;
	uint32_t *aa_r = a_r + RSANUMWORDS;
	uint32_t *aaa = aa_r;  /* Re-use location. */
	int i;

	/* Convert from big endian byte array to little endian word array. */
	for (i = 0; i < RSANUMWORDS; ++i) {
		uint32_t tmp =
			(inout[((RSANUMWORDS - 1 - i) * 4) + 0] << 24) |
			(inout[((RSANUMWORDS - 1 - i) * 4) + 1] << 16) |
			(inout[((RSANUMWORDS - 1 - i) * 4) + 2] << 8) |
			(inout[((RSANUMWORDS - 1 - i) * 4) + 3] << 0);
		a[i] = tmp;
	}

	mont_mul(key, a_r, a, key->rr);  /* a_r = a * RR / R mod M */
	for (i = 0; i < 16; i += 2) {
		mont_mul(key, aa_r, a_r, a_r); /* aa_r = a_r * a_r / R mod M */
		mont_mul(key, a_r, aa_r, aa_r);/* a_r = aa_r * aa_r / R mod M */
	}
	mont_mul(key, aaa, a_r, a);  /* aaa = a_r * a / R mod M */

	/* Make sure aaa < mod; aaa is at most 1x mod too large. */
	if (ge_mod(key, aaa))
		sub_mod(key, aaa);

	/* Convert to bigendian byte array */
	for (i = RSANUMWORDS - 1; i >= 0; --i) {
		uint32_t tmp = aaa[i];
		*inout++ = (uint8_t)(tmp >> 24);
		*inout++ = (uint8_t)(tmp >> 16);
		*inout++ = (uint8_t)(tmp >>  8);
		*inout++ = (uint8_t)(tmp >>  0);
	}
}

/*
 * PKCS#1 padding (from the RSA PKCS#1 v2.1 standard)
 *
 * The DER-encoded padding is defined as follows :
 * 0x00 || 0x01 || PS || 0x00 || T
 *
 * T: DER Encoded DigestInfo value which depends on the hash function used,
 * for SHA-256:
 * (0x)30 31 30 0d 06 09 60 86 48 01 65 03 04 02 01 05 00 04 20 || H.
 *
 * Length(T) = 51 octets for SHA-256
 *
 * PS: octet string consisting of {Length(RSA Key) - Length(T) - 3} 0xFF
 */
static const uint8_t sha256_tail[] = {
	0x00, 0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60,
	0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01,
	0x05, 0x00, 0x04, 0x20
};

#define PKCS_PAD_SIZE (RSANUMBYTES - SHA256_DIGEST_SIZE)

/**
 * Check PKCS#1 padding bytes
 *
 * @param sig  Signature to verify
 * @return 0 if the padding is correct.
 */
static int check_padding(const uint8_t *sig)
{
	uint8_t *ptr = (uint8_t *)sig;
	int result = 0;
	int i;

	/* First 2 bytes are always 0x00 0x01 */
	result |= *ptr++ ^ 0x00;
	result |= *ptr++ ^ 0x01;

	/* Then 0xff bytes until the tail */
	for (i = 0; i < PKCS_PAD_SIZE - sizeof(sha256_tail) - 2; i++)
		result |= *ptr++ ^ 0xff;

	/* Check the tail. */
	result |= memcmp(ptr, sha256_tail, sizeof(sha256_tail));

	return !!result;
}

/*
 * Verify a SHA256WithRSA PKCS#1 v1.5 signature against an expected
 * SHA256 hash.
 *
 * @param key           RSA public key
 * @param signature     RSA signature
 * @param sha           SHA-256 digest of the content to verify
 * @param workbuf32     Work buffer; caller must verify this is
 *                      3 x RSANUMWORDS elements long.
 * @return 0 on failure, 1 on success.
 */
int rsa_verify(const struct rsa_public_key *key, const uint8_t *signature,
	       const uint8_t *sha, uint32_t *workbuf32)
{
	uint8_t buf[RSANUMBYTES];

	/* Copy input to local workspace. */
	memcpy(buf, signature, RSANUMBYTES);

	mod_pow_F4(key, buf, workbuf32); /* In-place exponentiation. */

	/* Check the PKCS#1 padding */
	if (check_padding(buf) != 0)
		return 0;

	/* Check the digest. */
	if (memcmp(buf + PKCS_PAD_SIZE, sha, SHA256_DIGEST_SIZE) != 0)
		return 0;

	return 1;  /* All checked out OK. */
}
