/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test.h>

#include <openssl/crypto.h>
#include <openssl/rand.h>

ZTEST_SUITE(boringssl_crypto, NULL, NULL, NULL, NULL, NULL);

static ZTEST_DMEM volatile int expected_reason = -1;

void k_sys_fatal_error_handler(unsigned int reason, const z_arch_esf_t *pEsf)
{
	printk("Caught system error -- reason %d\n", reason);

	zassert_not_equal(expected_reason, -1, "Unexpected crash");
	zassert_equal(reason, expected_reason,
		      "Wrong crash type got %d expected %d\n", reason,
		      expected_reason);

	expected_reason = -1;
	ztest_test_pass();
}

ZTEST(boringssl_crypto, test_boringssl_self_test)
{
	zassert_equal(BORINGSSL_self_test(), 1, "BoringSSL self-test failed");
}

ZTEST(boringssl_crypto, test_rand)
{
	uint8_t zero[256] = { 0 };
	uint8_t buf1[256];
	uint8_t buf2[256];

	RAND_bytes(buf1, sizeof(buf1));
	RAND_bytes(buf2, sizeof(buf2));

	zassert_true(memcmp(buf1, zero, sizeof(zero)) != 0);
	zassert_true(memcmp(buf2, zero, sizeof(zero)) != 0);
	zassert_true(memcmp(buf1, buf2, sizeof(buf1)) != 0);
}

ZTEST(boringssl_crypto, test_rand_too_big_request)
{
	uint8_t *buffer = malloc(UINT16_MAX + 1);

	/*
	 * Requests bigger than UINT16_MAX causes k_oops() due to
	 * Zephyr Entropy API limits.
	 */
	expected_reason = K_ERR_KERNEL_OOPS;
	CRYPTO_sysrand(buffer, UINT16_MAX + 1);

	ztest_test_fail();
}
