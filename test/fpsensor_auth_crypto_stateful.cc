/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_commands.h"
#include "fpsensor_auth_crypto.h"
#include "fpsensor_state_without_driver_info.h"
#include "openssl/bn.h"
#include "openssl/ec.h"
#include "openssl/obj_mac.h"
#include "test_util.h"
#include "util.h"

#include <assert.h>
#include <stdbool.h>

#include <array>

namespace
{

void init_tpm_seed(void)
{
	std::array<uint8_t, 32> fake_tpm_seed = {
		0xd9, 0x71, 0xaf, 0xc4, 0xcd, 0x36, 0xe3, 0x60,
		0xf8, 0x5a, 0xa0, 0xa6, 0x2c, 0xb3, 0xf5, 0xe2,
		0xeb, 0xb9, 0xd8, 0x2f, 0xb5, 0x78, 0x5c, 0x79,
		0x82, 0xce, 0x06, 0x3f, 0xcc, 0x23, 0xb9, 0xe7,
	};

	static_assert(fake_tpm_seed.size() == sizeof(tpm_seed));

	std::copy(fake_tpm_seed.begin(), fake_tpm_seed.end(), tpm_seed);

	fp_encryption_status |= FP_ENC_STATUS_SEED_SET;
}

test_static enum ec_error_list test_fp_encrypt_decrypt_data(void)
{
	struct fp_auth_command_encryption_metadata info;
	const std::array<uint8_t, 32> input = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
						1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
						2, 3, 4, 5, 6, 7, 8, 9, 1, 2 };
	uint16_t version = 1;
	std::array<uint8_t, 32> data;

	std::copy(input.begin(), input.end(), data.begin());

	TEST_EQ(encrypt_data_in_place(version, info, data.data(), data.size()),
		EC_SUCCESS, "%d");

	TEST_EQ(info.struct_version, version, "%d");

	/* The encrypted data should not be the same as the input. */
	TEST_ASSERT_ARRAY_NE(data, input, data.size());

	/* TODO(crrev/c/4511815): Decrypt the data, and check the result is the
	 * same. */

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_encrypt_decrypt_key(void)
{
	uint16_t version = 1;
	std::array<uint8_t, 32> privkey = { 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
					    1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
					    2, 3, 4, 5, 6, 7, 8, 9, 1, 2 };

	bssl::UniquePtr<EC_KEY> key =
		create_ec_key_from_privkey(privkey.data(), privkey.size());

	TEST_NE(key.get(), nullptr, "%p");

	auto enc_key = create_encrypted_private_key(*key, version);
	TEST_ASSERT(enc_key.has_value());

	TEST_EQ(enc_key->info.struct_version, version, "%d");

	/* TODO(crrev/c/4511815): Decrypt the data, and check the result is the
	 * same. */

	return EC_SUCCESS;
}

} // namespace

extern "C" void run_test(int argc, const char **argv)
{
	init_tpm_seed();

	RUN_TEST(test_fp_encrypt_decrypt_data);
	RUN_TEST(test_fp_encrypt_decrypt_key);
	test_print_result();
}
