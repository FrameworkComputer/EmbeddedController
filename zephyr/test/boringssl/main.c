/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE /* Needed for getentropy */
#endif

#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test.h>

#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <unistd.h> /* getentropy */

void CRYPTO_sysrand(uint8_t *out, size_t requested);

ZTEST_SUITE(boringssl_crypto, NULL, NULL, NULL, NULL, NULL);

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

ZTEST(boringssl_crypto, test_rand_large_request)
{
	/*
	 * Requests bigger than UINT16_MAX are not supported
	 * by the Zephyr Entropy API. Make sure that BoringSSL can successfully
	 * request more.
	 */
	const int buf_size = UINT16_MAX + 1;
	uint8_t *buffer = calloc(buf_size, 1);
	uint8_t *zeroes = calloc(buf_size, 1);

	CRYPTO_sysrand(buffer, buf_size);
	zassert_true(memcmp(buffer, zeroes, buf_size) != 0);
}

ZTEST(boringssl_crypto, test_getentropy_too_large)
{
	uint8_t buf[256 + 1] = { 0 };

	int ret = getentropy(buf, sizeof(buf));
	zassert_equal(ret, -1);
	zassert_equal(errno, EIO);
}

ZTEST(boringssl_crypto, test_getentropy_null_buffer)
{
	int ret = getentropy(NULL, 0);
	zassert_equal(ret, -1);
	zassert_equal(errno, EFAULT);
}

ZTEST(boringssl_crypto, test_getentropy)
{
	uint8_t zero[256] = { 0 };
	uint8_t buf1[256];
	uint8_t buf2[256];

	int ret = getentropy(buf1, sizeof(buf1));
	zassert_equal(ret, 0);

	ret = getentropy(buf2, sizeof(buf2));
	zassert_equal(ret, 0);

	zassert_true(memcmp(buf1, zero, sizeof(zero)) != 0);
	zassert_true(memcmp(buf2, zero, sizeof(zero)) != 0);
	zassert_true(memcmp(buf1, buf2, sizeof(buf1)) != 0);
}
