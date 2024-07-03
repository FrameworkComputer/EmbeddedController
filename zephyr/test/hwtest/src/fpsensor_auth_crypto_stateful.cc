/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_auth_crypto.h"
#include "mock/otpi_mock.h"
#include "openssl/bn.h"
#include "openssl/ec.h"
#include "openssl/obj_mac.h"
#include "otp_key.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/ztest.h>

#include <algorithm>
#include <array>

constexpr std::array<uint8_t, 32> kFakeTpmSeed = {
	0xd9, 0x71, 0xaf, 0xc4, 0xcd, 0x36, 0xe3, 0x60, 0xf8, 0x5a, 0xa0,
	0xa6, 0x2c, 0xb3, 0xf5, 0xe2, 0xeb, 0xb9, 0xd8, 0x2f, 0xb5, 0x78,
	0x5c, 0x79, 0x82, 0xce, 0x06, 0x3f, 0xcc, 0x23, 0xb9, 0xe7,
};
static_assert(kFakeTpmSeed.size() == FP_CONTEXT_TPM_BYTES);

constexpr std::array<uint8_t, 32> kFakeUserId = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05,  0x06, 0x07,  0x08, 0x09, 0x0a,
	0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x010, 0x11, 0x012, 0x13, 0x14, 0x15,
	0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b,  0x1c, 0x1d,  0x1e, 0x1f
};
static_assert(kFakeUserId.size() == FP_CONTEXT_USERID_BYTES);

void *setup_suite(void)
{
	if (IS_ENABLED(CONFIG_OTP_KEY)) {
		std::ranges::copy(default_fake_otp_key,
				  mock_otp.otp_key_buffer);
	}

	return NULL;
}

ZTEST_SUITE(fpsensor_auth_crypto_stateful, NULL, setup_suite, NULL, NULL, NULL);

ZTEST(fpsensor_auth_crypto_stateful, test_fp_encrypt_decrypt_data)
{
	struct fp_auth_command_encryption_metadata info;
	const std::array<uint8_t, 32> input = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
						1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
						2, 3, 4, 5, 6, 7, 8, 9, 1, 2 };
	uint16_t version = 1;
	std::array<uint8_t, 32> data = input;

	zassert_equal(encrypt_data_in_place(version, info, kFakeUserId,
					    kFakeTpmSeed, data),
		      EC_SUCCESS);

	zassert_equal(info.struct_version, version, "%d");

	/* The encrypted data should not be the same as the input. */
	zassert_false(memcmp(data.data(), input.data(), data.size()) == 0);

	std::array<uint8_t, 32> output;
	zassert_equal(decrypt_data(info, kFakeUserId, kFakeTpmSeed, data,
				   output),
		      EC_SUCCESS);

	zassert_mem_equal(input.data(), output.data(), sizeof(input));
}

ZTEST(fpsensor_auth_crypto_stateful, test_fp_encrypt_decrypt_key)
{
	uint16_t version = 1;
	std::array<uint8_t, 32> privkey = { 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
					    1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
					    2, 3, 4, 5, 6, 7, 8, 9, 1, 2 };

	bssl::UniquePtr<EC_KEY> key =
		create_ec_key_from_privkey(privkey.data(), privkey.size());

	zassert_not_equal(key.get(), nullptr, "%p");

	auto enc_key = create_encrypted_private_key(*key, version, kFakeUserId,
						    kFakeTpmSeed);
	zassert_true(enc_key.has_value());

	zassert_equal(enc_key->info.struct_version, version, "%d");

	bssl::UniquePtr<EC_KEY> out_key =
		decrypt_private_key(*enc_key, kFakeUserId, kFakeTpmSeed);

	zassert_not_equal(key.get(), nullptr, "%p");

	std::array<uint8_t, 32> output_privkey;
	EC_KEY_priv2oct(out_key.get(), output_privkey.data(),
			output_privkey.size());

	zassert_mem_equal(privkey.data(), output_privkey.data(),
			  sizeof(privkey));
}
