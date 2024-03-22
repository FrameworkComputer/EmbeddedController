/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_auth_crypto.h"
#include "openssl/bn.h"
#include "openssl/ec.h"
#include "openssl/obj_mac.h"
#include "test_util.h"
#include "util.h"

#include <assert.h>
#include <stdbool.h>

#include <array>

extern "C" enum ec_error_list rollback_get_secret(uint8_t *secret)
{
	// We should not call this function in the test.
	TEST_ASSERT(false);
}

namespace
{

test_static enum ec_error_list test_fp_create_ec_key_from_pubkey(void)
{
	fp_elliptic_curve_public_key pubkey = {
		.x = {
			0x85, 0xAD, 0x35, 0x23, 0x05, 0x1E, 0x33, 0x3F,
			0xCA, 0xA7, 0xEA, 0xA5, 0x88, 0x33, 0x12, 0x95,
			0xA7, 0xB5, 0x98, 0x9F, 0x32, 0xEF, 0x7D, 0xE9,
			0xF8, 0x70, 0x14, 0x5E, 0x89, 0xCB, 0xDE, 0x1F,
		},
		.y = {
			0xD1, 0xDC, 0x91, 0xC6, 0xE6, 0x5B, 0x1E, 0x3C,
			0x01, 0x6C, 0xE6, 0x50, 0x25, 0x5D, 0x89, 0xCF,
			0xB7, 0x8D, 0x88, 0xB9, 0x0D, 0x09, 0x41, 0xF1,
			0x09, 0x4F, 0x61, 0x55, 0x6C, 0xC4, 0x96, 0x6B,
		},
	};

	bssl::UniquePtr<EC_KEY> key = create_ec_key_from_pubkey(pubkey);

	TEST_NE(key.get(), nullptr, "%p");
	TEST_EQ(EC_KEY_check_key(key.get()), 1, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_create_ec_key_from_pubkey_fail(void)
{
	fp_elliptic_curve_public_key pubkey = {
		.x = {},
		.y = {},
	};

	bssl::UniquePtr<EC_KEY> key = create_ec_key_from_pubkey(pubkey);

	TEST_EQ(key.get(), nullptr, "%p");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_create_pubkey_from_ec_key(void)
{
	fp_elliptic_curve_public_key pubkey = {
		.x = {
			0x85, 0xAD, 0x35, 0x23, 0x05, 0x1E, 0x33, 0x3F,
			0xCA, 0xA7, 0xEA, 0xA5, 0x88, 0x33, 0x12, 0x95,
			0xA7, 0xB5, 0x98, 0x9F, 0x32, 0xEF, 0x7D, 0xE9,
			0xF8, 0x70, 0x14, 0x5E, 0x89, 0xCB, 0xDE, 0x1F,
		},
		.y = {
			0xD1, 0xDC, 0x91, 0xC6, 0xE6, 0x5B, 0x1E, 0x3C,
			0x01, 0x6C, 0xE6, 0x50, 0x25, 0x5D, 0x89, 0xCF,
			0xB7, 0x8D, 0x88, 0xB9, 0x0D, 0x09, 0x41, 0xF1,
			0x09, 0x4F, 0x61, 0x55, 0x6C, 0xC4, 0x96, 0x6B,
		},
	};

	bssl::UniquePtr<EC_KEY> key = create_ec_key_from_pubkey(pubkey);

	TEST_NE(key.get(), nullptr, "%p");
	TEST_EQ(EC_KEY_check_key(key.get()), 1, "%d");

	auto result = create_pubkey_from_ec_key(*key);
	TEST_ASSERT(result.has_value());

	TEST_ASSERT_ARRAY_EQ(result->x, pubkey.x, sizeof(pubkey.x));
	TEST_ASSERT_ARRAY_EQ(result->y, pubkey.y, sizeof(pubkey.y));

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_create_ec_key_from_privkey(void)
{
	std::array<uint8_t, 32> data = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
					 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
					 2, 3, 4, 5, 6, 7, 8, 9, 1, 2 };

	bssl::UniquePtr<EC_KEY> key =
		create_ec_key_from_privkey(data.data(), data.size());

	TEST_NE(key.get(), nullptr, "%p");

	/* There is nothing to check for the private key. */

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_create_ec_key_from_privkey_fail(void)
{
	std::array<uint8_t, 1> data = {};

	bssl::UniquePtr<EC_KEY> key =
		create_ec_key_from_privkey(data.data(), data.size());

	TEST_EQ(key.get(), nullptr, "%p");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_generate_ecdh_shared_secret(void)
{
	struct fp_elliptic_curve_public_key pubkey = {
		.x = {
			0x85, 0xAD, 0x35, 0x23, 0x05, 0x1E, 0x33, 0x3F,
			0xCA, 0xA7, 0xEA, 0xA5, 0x88, 0x33, 0x12, 0x95,
			0xA7, 0xB5, 0x98, 0x9F, 0x32, 0xEF, 0x7D, 0xE9,
			0xF8, 0x70, 0x14, 0x5E, 0x89, 0xCB, 0xDE, 0x1F,
		},
		.y = {
			0xD1, 0xDC, 0x91, 0xC6, 0xE6, 0x5B, 0x1E, 0x3C,
			0x01, 0x6C, 0xE6, 0x50, 0x25, 0x5D, 0x89, 0xCF,
			0xB7, 0x8D, 0x88, 0xB9, 0x0D, 0x09, 0x41, 0xF1,
			0x09, 0x4F, 0x61, 0x55, 0x6C, 0xC4, 0x96, 0x6B,
		},
	};

	bssl::UniquePtr<EC_KEY> public_key = create_ec_key_from_pubkey(pubkey);

	TEST_NE(public_key.get(), nullptr, "%p");

	std::array<uint8_t, 32> privkey = { 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
					    1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
					    2, 3, 4, 5, 6, 7, 8, 9, 1, 2 };

	bssl::UniquePtr<EC_KEY> private_key =
		create_ec_key_from_privkey(privkey.data(), privkey.size());

	TEST_NE(private_key.get(), nullptr, "%p");

	std::array<uint8_t, 32> shared_secret;
	TEST_EQ(generate_ecdh_shared_secret(*private_key, *public_key,
					    shared_secret.data(),
					    shared_secret.size()),
		EC_SUCCESS, "%d");

	std::array<uint8_t, 32> expected_result = {
		0x46, 0x86, 0xca, 0x75, 0xce, 0xa1, 0xde, 0x23,
		0x48, 0xb3, 0x0b, 0xfc, 0xd7, 0xbe, 0x7a, 0xa0,
		0x33, 0x17, 0x6c, 0x97, 0xc6, 0xa7, 0x70, 0x7c,
		0xd4, 0x2c, 0xfd, 0xc0, 0xba, 0xc1, 0x47, 0x01,
	};

	TEST_ASSERT_ARRAY_EQ(shared_secret, expected_result,
			     shared_secret.size());
	return EC_SUCCESS;
}

} // namespace

extern "C" void run_test(int argc, const char **argv)
{
	RUN_TEST(test_fp_create_ec_key_from_pubkey);
	RUN_TEST(test_fp_create_ec_key_from_pubkey_fail);
	RUN_TEST(test_fp_create_ec_key_from_privkey);
	RUN_TEST(test_fp_create_ec_key_from_privkey_fail);
	RUN_TEST(test_fp_create_pubkey_from_ec_key);
	RUN_TEST(test_fp_generate_ecdh_shared_secret);
	test_print_result();
}
