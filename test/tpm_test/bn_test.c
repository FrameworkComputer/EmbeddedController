/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "dcrypto.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>

#include <openssl/bn.h>

static int test_bn_modinv_helper(const BIGNUM *E, BN_CTX *ctx)
{
	int i;
	BIGNUM *MOD = BN_CTX_get(ctx);

	for (i = 0; i < 1000; i++) {

		uint32_t m_buf[64];
		uint32_t d_buf[64];
		uint32_t e_buf[32];
		int has_inverse;
		int test_inverse;

		struct LITE_BIGNUM m;
		struct LITE_BIGNUM e;
		struct LITE_BIGNUM d;

		BIGNUM *r = BN_CTX_get(ctx);

		memset(e_buf, 0, sizeof(e_buf));

		/* Top bit set, bottom bit clear. */
		BN_rand(MOD, 2048, 1, 0);

		if (BN_mod_inverse(r, E, MOD, ctx))
			has_inverse = 1;
		else
			has_inverse = 0;

		DCRYPTO_bn_wrap(&m, m_buf, sizeof(m_buf));
		memcpy(m_buf, MOD->d, sizeof(m_buf));
		assert(BN_num_bytes(E) <= sizeof(e_buf));
		memcpy(e_buf, E->d, BN_num_bytes(E));

		DCRYPTO_bn_wrap(&e, e_buf, sizeof(e_buf));
		bn_init(&d, d_buf, sizeof(d_buf));

		test_inverse = bn_modinv_vartime(&d, &e, &m);

		if (test_inverse != has_inverse) {
			fprintf(stderr,
				"ossl inverse: %d, dcrypto inverse: %d\n",
				has_inverse, test_inverse);
			fprintf(stderr, "d : ");
			BN_print_fp(stderr, r);
			fprintf(stderr, "\n");

			fprintf(stderr, "e : ");
			BN_print_fp(stderr, E);
			fprintf(stderr, "\n");

			fprintf(stderr, "M : ");
			BN_print_fp(stderr, MOD);
			fprintf(stderr, "\n");

			return 1;
		}

		if (has_inverse) {
			if (memcmp(d.d, r->d, BN_num_bytes(r)) != 0) {
				fprintf(stderr, "memcmp fail\n");
				return 1;
			}
		}

		BN_free(r);
	}

	return 0;
}

static int test_bn_modinv(void)
{
	int result = 1;
	BN_CTX *ctx = BN_CTX_new();

	BN_CTX_start(ctx);

	BIGNUM *E = BN_CTX_get(ctx);

	BN_rand(E, 1024, 1, 1);
	if (test_bn_modinv_helper(E, ctx))
		goto fail;

	BN_rand(E, 1024, 1, 0);
	if (test_bn_modinv_helper(E, ctx))
		goto fail;

	BN_set_word(E, 3);
	if (test_bn_modinv_helper(E, ctx))
		goto fail;

	BN_set_word(E, 65537);
	if (test_bn_modinv_helper(E, ctx))
		goto fail;

	result = 0;
fail:
	BN_CTX_end(ctx);
	BN_CTX_free(ctx);
	return result;
}

void *always_memset(void *s, int c, size_t n)
{
	memset(s, c, n);
	return s;
}

void watchdog_reload(void)
{
}

int main(void)
{
	assert(test_bn_modinv() == 0);
	fprintf(stderr, "PASS\n");
	return 0;
}
