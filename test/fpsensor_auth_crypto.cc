/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_commands.h"
#include "fpsensor_auth_crypto.h"
#include "openssl/bn.h"
#include "openssl/ec.h"
#include "openssl/obj_mac.h"
#include "test_util.h"
#include "util.h"

#include <stdbool.h>

#include <array>

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

} // namespace

extern "C" void run_test(int argc, const char **argv)
{
	RUN_TEST(test_fp_create_ec_key_from_pubkey);
	RUN_TEST(test_fp_create_ec_key_from_pubkey_fail);
	RUN_TEST(test_fp_create_pubkey_from_ec_key);
	test_print_result();
}
