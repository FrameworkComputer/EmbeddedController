/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>

#include "dcrypto.h"

/* Compute k based on a given {key, digest} pair, 0 < k < n. */
static void determine_k(const p256_int *key, const p256_int *digest,
			char *tweak, p256_int *k)
{
	do {
		p256_int p1, p2;
		struct HMAC_CTX hmac;

		/* NOTE: taking the p256_int in-memory representation
		 * is not endian neutral.  Signatures with an
		 * identical key on identical digests will differ per
		 * host endianness.  This however does not jeopardize
		 * the key bits. */
		dcrypto_HMAC_SHA256_init(&hmac, key, P256_NBYTES);
		dcrypto_HMAC_update(&hmac, tweak, 1);
		dcrypto_HMAC_update(&hmac, (uint8_t *) digest, P256_NBYTES);
		++(*tweak);
		p256_from_bin(dcrypto_HMAC_final(&hmac), &p1);

		dcrypto_HMAC_SHA256_init(&hmac, key, P256_NBYTES);
		dcrypto_HMAC_update(&hmac, tweak, 1);
		dcrypto_HMAC_update(&hmac, (uint8_t *) digest, P256_NBYTES);
		++(*tweak);
		p256_from_bin(dcrypto_HMAC_final(&hmac), &p2);

		/* Combine p1 and p2 into well distributed k. */
		p256_modmul(&SECP256r1_n, &p1, 0, &p2, k);

		/* (Attempt to) clear stack state. */
		p256_clear(&p1);
		p256_clear(&p2);

	} while (p256_is_zero(k));
}

void DCRYPTO_p256_ecdsa_sign(const p256_int *key, const p256_int *digest,
			p256_int *r, p256_int *s)
{
	char tweak = 'A';
	p256_digit top;

	for (;;) {
		p256_int k, kinv;

		determine_k(key, digest, &tweak, &k);
		DCRYPTO_p256_base_point_mul(r, s, &k);
		p256_mod(&SECP256r1_n, r, r);

		/* Make sure r != 0. */
		if (p256_is_zero(r))
			continue;

		p256_modmul(&SECP256r1_n, r, 0, key, s);
		top = p256_add(s, digest, s);
		p256_modinv(&SECP256r1_n, &k, &kinv);
		p256_modmul(&SECP256r1_n, &kinv, top, s, s);

		/* (Attempt to) clear stack state. */
		p256_clear(&k);
		p256_clear(&kinv);

		/* Make sure s != 0. */
		if (p256_is_zero(s))
			continue;

		break;
	}
}

int DCRYPTO_p256_ecdsa_verify(const p256_int *key_x, const p256_int *key_y,
			const p256_int *digest,
			const p256_int *r, const p256_int *s)
{
	p256_int u, v;

	/* Check public key. */
	if (!DCRYPTO_p256_valid_point(key_x, key_y))
		return 0;

	/* Check r and s are != 0 % n. */
	p256_mod(&SECP256r1_n, r, &u);
	p256_mod(&SECP256r1_n, s, &v);
	if (p256_is_zero(&u) || p256_is_zero(&v))
		return 0;

	p256_modinv_vartime(&SECP256r1_n, s, &v);
	p256_modmul(&SECP256r1_n, digest, 0, &v, &u);  /* digest / s % n */
	p256_modmul(&SECP256r1_n, r, 0, &v, &v);  /* r / s % n */

	p256_points_mul_vartime(&u, &v,	key_x, key_y, &u, &v);

	p256_mod(&SECP256r1_n, &u, &u);  /* (x coord % p) % n */
	return p256_cmp(r, &u) == 0;
}
