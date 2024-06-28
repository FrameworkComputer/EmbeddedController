/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "crypto/cleanse_wrapper.h"
#include "crypto/elliptic_curve_key.h"
#include "openssl/bn.h"
#include "openssl/ec.h"
#include "openssl/mem.h"
#include "openssl/obj_mac.h"
#include "openssl/rand.h"
#include "sha256.h"

#include <zephyr/ztest.h>

#include <array>
#include <memory>
#include <unistd.h>

ZTEST_SUITE(boringssl_crypto, NULL, NULL, NULL, NULL, NULL);

ZTEST(boringssl_crypto, test_rand)
{
	constexpr std::array<uint8_t, 256> zero{};
	std::array<uint8_t, 256> buf1{};
	std::array<uint8_t, 256> buf2{};

	RAND_bytes(buf1.data(), buf1.size());
	RAND_bytes(buf2.data(), buf2.size());

	zassert_true(buf1 != zero);
	zassert_true(buf2 != zero);
	zassert_true(buf1 != buf2);
}

ZTEST(boringssl_crypto, test_ecc_keygen)
{
	bssl::UniquePtr<EC_KEY> key1 = generate_elliptic_curve_key();

	zassert_not_equal(key1.get(), nullptr, "%p");

	/* The generated key should be valid.*/
	zassert_equal(EC_KEY_check_key(key1.get()), 1, "%d");

	bssl::UniquePtr<EC_KEY> key2 = generate_elliptic_curve_key();

	zassert_not_equal(key2.get(), nullptr, "%p");

	/* The generated key should be valid. */
	zassert_equal(EC_KEY_check_key(key2.get()), 1, "%d");

	const BIGNUM *priv1 = EC_KEY_get0_private_key(key1.get());
	const BIGNUM *priv2 = EC_KEY_get0_private_key(key2.get());

	/* The generated keys should not be the same. */
	zassert_not_equal(BN_cmp(priv1, priv2), 0, "%d");

	/* The generated keys should not be zero. */
	zassert_equal(BN_is_zero(priv1), 0, "%d");
	zassert_equal(BN_is_zero(priv2), 0, "%d");
}

ZTEST(boringssl_crypto, test_cleanse_wrapper_std_array)
{
	using TypeToCheck = std::array<uint8_t, 6>;
	using Wrapped = CleanseWrapper<TypeToCheck>;

	/* Preserve a space for it. */
	uint8_t *buffer = static_cast<uint8_t *>(malloc(sizeof(Wrapped)));
	Wrapped *data = reinterpret_cast<Wrapped *>(buffer);

	/* Call the constructor. */
	new (data) Wrapped({ 1, 1, 1, 1, 1, 1 });

	for (const auto &item : *data) {
		zassert_equal(item, 1);
	}

	/* Call the destructor. */
	data->~Wrapped();

	for (int i = 0; i < sizeof(Wrapped); i++) {
		zassert_true(buffer[i] == 0);
	}

	/* Free the space. */
	free(buffer);
}

ZTEST(boringssl_crypto, test_cleanse_wrapper_sha256)
{
	using TypeToCheck = struct sha256_ctx;
	using Wrapped = CleanseWrapper<TypeToCheck>;

	/* Preserve a space for it. */
	uint8_t *buffer = static_cast<uint8_t *>(malloc(sizeof(Wrapped)));
	Wrapped *data = reinterpret_cast<Wrapped *>(buffer);

	/* Call the constructor. */
	new (data) Wrapped();

	std::array<uint8_t, 5> data_to_sha = { 1, 2, 3, 4, 5 };
	SHA256_init(data);
	SHA256_update(data, data_to_sha.data(), data_to_sha.size());
	uint8_t *result = SHA256_final(data);

	std::array<uint8_t, 32> expected_result = {
		0X74, 0XF8, 0X1F, 0XE1, 0X67, 0XD9, 0X9B, 0X4C,
		0XB4, 0X1D, 0X6D, 0X0C, 0XCD, 0XA8, 0X22, 0X78,
		0XCA, 0XEE, 0X9F, 0X3E, 0X2F, 0X25, 0XD5, 0XE5,
		0XA3, 0X93, 0X6F, 0XF3, 0XDC, 0XEC, 0X60, 0XD0,
	};

	zassert_mem_equal(result, expected_result.data(),
			  expected_result.size());

	/* Call the destructor. */
	data->~Wrapped();

	for (int i = 0; i < sizeof(Wrapped); i++) {
		zassert_true(buffer[i] == 0);
	}

	/* Free the space. */
	free(buffer);
}

ZTEST(boringssl_crypto, test_cleanse_wrapper_custom_struct)
{
	struct TestingStruct {
		bool used;
		std::array<uint32_t, 4> data;
	};

	using TypeToCheck = TestingStruct;
	using Wrapped = CleanseWrapper<TypeToCheck>;

	/* Preserve a space for it. */
	uint8_t *buffer = static_cast<uint8_t *>(malloc(sizeof(Wrapped)));
	Wrapped *data = reinterpret_cast<Wrapped *>(buffer);

	/* Call the constructor. */
	new (data) Wrapped({
		.used = true,
		.data = { 0x7fffffffu, 0x12345678u, 0x0u, 0x42u },
	});

	zassert_equal(data->used, true, "%d");
	zassert_equal(data->data[0], 0x7fffffffu, "%d");
	zassert_equal(data->data[1], 0x12345678u, "%d");
	zassert_equal(data->data[2], 0x0u, "%d");
	zassert_equal(data->data[3], 0x42u, "%d");

	/* Call the destructor. */
	data->~Wrapped();

	for (int i = 0; i < sizeof(Wrapped); i++) {
		zassert_true(buffer[i] == 0);
	}

	/* Free the space. */
	free(buffer);
}

ZTEST(boringssl_crypto, test_cleanse_wrapper_normal_usage)
{
	struct TestingStruct {
		bool used;
		std::array<uint32_t, 4> data;
	};

	CleanseWrapper<std::array<uint8_t, 6> > array({ 1, 1, 1, 1, 1, 1 });

	for (const auto &item : array) {
		zassert_equal(item, 1);
	}

	CleanseWrapper<TestingStruct> data({
		.used = true,
		.data = { 0x7fffffffu, 0x12345678u, 0x0u, 0x42u },
	});

	zassert_equal(data.used, true, "%d");
	zassert_equal(data.data[0], 0x7fffffffu, "%d");
	zassert_equal(data.data[1], 0x12345678u, "%d");
	zassert_equal(data.data[2], 0x0u, "%d");
	zassert_equal(data.data[3], 0x42u, "%d");

	CleanseWrapper<struct sha256_ctx> ctx;

	std::array<uint8_t, 5> data_to_sha = { 1, 2, 3, 4, 5 };
	SHA256_init(&ctx);
	SHA256_update(&ctx, data_to_sha.data(), data_to_sha.size());
	uint8_t *result = SHA256_final(&ctx);

	std::array<uint8_t, 32> expected_result = {
		0X74, 0XF8, 0X1F, 0XE1, 0X67, 0XD9, 0X9B, 0X4C,
		0XB4, 0X1D, 0X6D, 0X0C, 0XCD, 0XA8, 0X22, 0X78,
		0XCA, 0XEE, 0X9F, 0X3E, 0X2F, 0X25, 0XD5, 0XE5,
		0XA3, 0X93, 0X6F, 0XF3, 0XDC, 0XEC, 0X60, 0XD0,
	};

	zassert_mem_equal(result, expected_result.data(),
			  expected_result.size());

	/* There is no way to check the context is cleared without undefined
	 * behavior. */
}

ZTEST(boringssl_crypto, test_getentropy_too_large)
{
	std::array<uint8_t, 256 + 1> buf{};

	int ret = getentropy(buf.data(), buf.size());
	zassert_equal(ret, -1, "%d");
	zassert_equal(errno, EIO, "%d");
}

ZTEST(boringssl_crypto, test_getentropy_null_buffer)
{
	int ret = getentropy(nullptr, 0);
	zassert_equal(ret, -1, "%d");
	zassert_equal(errno, EFAULT, "%d");
}

ZTEST(boringssl_crypto, test_getentropy)
{
	constexpr std::array<uint8_t, 256> zero{};
	std::array<uint8_t, 256> buf1{};
	std::array<uint8_t, 256> buf2{};

	int ret = getentropy(buf1.data(), buf1.size());
	zassert_equal(ret, 0, "%d");

	ret = getentropy(buf2.data(), buf2.size());
	zassert_equal(ret, 0, "%d");

	zassert_true(buf1 != zero);
	zassert_true(buf2 != zero);

	/* The host TRNG (chip/host/trng.c) is deterministic for testing. */
	if (IS_ENABLED(BOARD_HOST)) {
		zassert_true(buf1 == buf2);
	} else {
		zassert_true(buf1 != buf2);
	}
}
