/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "crypto/elliptic_curve_key.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_auth_crypto.h"
#include "openssl/aes.h"
#include "openssl/bn.h"
#include "openssl/ec.h"
#include "openssl/obj_mac.h"
#include "test_util.h"
#include "util.h"

#include <assert.h>
#include <stdbool.h>

#include <algorithm>
#include <array>

namespace
{

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

test_static enum ec_error_list test_fp_encrypt_decrypt_data(void)
{
	struct fp_auth_command_encryption_metadata info;
	const std::array<uint8_t, 32> input = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
						1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
						2, 3, 4, 5, 6, 7, 8, 9, 1, 2 };
	uint16_t version = 1;
	std::array<uint8_t, 32> data = input;

	TEST_EQ(encrypt_data_in_place(version, info, kFakeUserId, kFakeTpmSeed,
				      data),
		EC_SUCCESS, "%d");

	TEST_EQ(info.struct_version, version, "%d");

	/* The encrypted data should not be the same as the input. */
	TEST_ASSERT_ARRAY_NE(data, input, data.size());

	std::array<uint8_t, 32> output;
	TEST_EQ(decrypt_data(info, kFakeUserId, kFakeTpmSeed, data, output),
		EC_SUCCESS, "%d");

	TEST_ASSERT_ARRAY_EQ(input, output, sizeof(input));

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

	auto enc_key = create_encrypted_private_key(*key, version, kFakeUserId,
						    kFakeTpmSeed);
	TEST_ASSERT(enc_key.has_value());

	TEST_EQ(enc_key->info.struct_version, version, "%d");

	bssl::UniquePtr<EC_KEY> out_key =
		decrypt_private_key(*enc_key, kFakeUserId, kFakeTpmSeed);

	TEST_NE(key.get(), nullptr, "%p");

	std::array<uint8_t, 32> output_privkey;
	EC_KEY_priv2oct(out_key.get(), output_privkey.data(),
			output_privkey.size());

	TEST_ASSERT_ARRAY_EQ(privkey, output_privkey, sizeof(privkey));

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_generate_gsc_session_key(void)
{
	std::array<uint8_t, 32> auth_nonce = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
					       1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
					       2, 3, 4, 5, 6, 7, 8, 9, 1, 2 };
	std::array<uint8_t, 32> gsc_nonce = { 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
					      1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
					      2, 3, 4, 5, 6, 7, 8, 9, 1, 2 };
	std::array<uint8_t, 32> pairing_key = { 2, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
						1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
						2, 3, 4, 5, 6, 7, 8, 9, 1, 2 };

	std::array<uint8_t, 32> gsc_session_key;

	TEST_EQ(generate_gsc_session_key(auth_nonce, gsc_nonce, pairing_key,
					 gsc_session_key),
		EC_SUCCESS, "%d");

	std::array<uint8_t, 32> expected_gsc_session_key = {
		0X1A, 0X1A, 0X3C, 0X33, 0X7F, 0XAE, 0XF9, 0X3E,
		0XA8, 0X7C, 0XE4, 0XEC, 0XD9, 0XFF, 0X45, 0X8A,
		0XB6, 0X2F, 0X75, 0XD5, 0XEA, 0X25, 0X93, 0X36,
		0X60, 0XF1, 0XAB, 0XD2, 0XF4, 0X9F, 0X22, 0X89,
	};

	TEST_ASSERT_ARRAY_EQ(gsc_session_key, expected_gsc_session_key,
			     gsc_session_key.size());

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_generate_gsc_session_key_fail(void)
{
	std::array<uint8_t, 32> auth_nonce = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
					       1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
					       2, 3, 4, 5, 6, 7, 8, 9, 1, 2 };
	std::array<uint8_t, 32> gsc_nonce = { 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
					      1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
					      2, 3, 4, 5, 6, 7, 8, 9, 1, 2 };
	std::array<uint8_t, 32> pairing_key = { 2, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
						1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
						2, 3, 4, 5, 6, 7, 8, 9, 1, 2 };

	/* Wrong gsc_session_key size. */
	std::array<uint8_t, 30> gsc_session_key;

	TEST_NE(generate_gsc_session_key(auth_nonce, gsc_nonce, pairing_key,
					 gsc_session_key),
		EC_SUCCESS, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list
test_fp_decrypt_data_with_gsc_session_key_in_place(void)
{
	std::array<uint8_t, 32> gsc_session_key = {
		0X1A, 0X1A, 0X3C, 0X33, 0X7F, 0XAE, 0XF9, 0X3E,
		0XA8, 0X7C, 0XE4, 0XEC, 0XD9, 0XFF, 0X45, 0X8A,
		0XB6, 0X2F, 0X75, 0XD5, 0XEA, 0X25, 0X93, 0X36,
		0X60, 0XF1, 0XAB, 0XD2, 0XF4, 0X9F, 0X22, 0X89,
	};

	std::array<uint8_t, 16> iv = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
	};

	std::array<uint8_t, 32> data = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
					 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
					 2, 3, 4, 5, 6, 7, 8, 9, 1, 2 };

	TEST_EQ(decrypt_data_with_gsc_session_key_in_place(gsc_session_key, iv,
							   data),
		EC_SUCCESS, "%d");

	std::array<uint8_t, 32> expected_data = {
		0X6D, 0XED, 0XAD, 0X04, 0XF8, 0XDB, 0XAE, 0X51,
		0XF8, 0XEE, 0X94, 0X7E, 0XDB, 0X12, 0X14, 0X22,
		0X38, 0X32, 0X27, 0XC5, 0X19, 0X72, 0XA3, 0X60,
		0X67, 0X71, 0X25, 0XE8, 0X27, 0X56, 0XC6, 0X35,
	};

	TEST_ASSERT_ARRAY_EQ(data, expected_data, data.size());

	return EC_SUCCESS;
}

test_static enum ec_error_list
test_fp_decrypt_data_with_gsc_session_key_in_place_fail(void)
{
	std::array<uint8_t, 32> gsc_session_key = {
		0X1A, 0X1A, 0X3C, 0X33, 0X7F, 0XAE, 0XF9, 0X3E,
		0XA8, 0X7C, 0XE4, 0XEC, 0XD9, 0XFF, 0X45, 0X8A,
		0XB6, 0X2F, 0X75, 0XD5, 0XEA, 0X25, 0X93, 0X36,
		0X60, 0XF1, 0XAB, 0XD2, 0XF4, 0X9F, 0X22, 0X89,
	};

	/* Wrong IV size. */
	std::array<uint8_t, 32> iv = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
	};

	std::array<uint8_t, 32> data = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
					 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
					 2, 3, 4, 5, 6, 7, 8, 9, 1, 2 };

	TEST_NE(decrypt_data_with_gsc_session_key_in_place(gsc_session_key, iv,
							   data),
		EC_SUCCESS, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_encrypt_data_with_ecdh_key_in_place(void)
{
	constexpr std::array<uint8_t, FP_ELLIPTIC_CURVE_PUBLIC_KEY_IV_LEN>
		zero_iv = { 0 };

	bssl::UniquePtr<EC_KEY> ecdh_key = generate_elliptic_curve_key();

	std::optional<fp_elliptic_curve_public_key> pubkey =
		create_pubkey_from_ec_key(*ecdh_key);

	TEST_ASSERT(pubkey.has_value());

	TEST_NE(ecdh_key.get(), nullptr, "%p");

	struct fp_elliptic_curve_public_key response_pubkey;

	std::array<uint8_t, FP_POSITIVE_MATCH_SECRET_BYTES> secret = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
		6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 1, 2
	};

	std::array<uint8_t, FP_POSITIVE_MATCH_SECRET_BYTES> enc_secret = secret;

	std::array<uint8_t, FP_ELLIPTIC_CURVE_PUBLIC_KEY_IV_LEN> iv = {};

	TEST_ASSERT_ARRAY_EQ(iv, zero_iv, iv.size());

	TEST_EQ(encrypt_data_with_ecdh_key_in_place(*pubkey, enc_secret, iv,
						    response_pubkey),
		EC_SUCCESS, "%d");

	/* The encrypted data should not be the same as the input. */
	TEST_ASSERT_ARRAY_NE(enc_secret, secret, secret.size());

	/* The IV should not be zero. */
	TEST_ASSERT_ARRAY_NE(iv, zero_iv, iv.size());

	bssl::UniquePtr<EC_KEY> output_key =
		create_ec_key_from_pubkey(response_pubkey);

	TEST_NE(output_key.get(), nullptr, "%p");

	std::array<uint8_t, 32> share_secret;
	TEST_EQ(generate_ecdh_shared_secret(*ecdh_key, *output_key,
					    share_secret.data(),
					    share_secret.size()),
		EC_SUCCESS, "%d");

	AES_KEY aes_key;
	TEST_EQ(AES_set_encrypt_key(share_secret.data(), 256, &aes_key), 0,
		"%d");

	unsigned int block_num = 0;
	std::array<uint8_t, AES_BLOCK_SIZE> ecount_buf;

	/* The AES CTR uses the same function for encryption & decryption. */
	AES_ctr128_encrypt(enc_secret.data(), enc_secret.data(),
			   enc_secret.size(), &aes_key, iv.data(),
			   ecount_buf.data(), &block_num);

	/* The secret should be the same after decrypt. */
	TEST_ASSERT_ARRAY_EQ(enc_secret, secret, secret.size());

	return EC_SUCCESS;
}

} // namespace

void run_test(int argc, const char **argv)
{
	RUN_TEST(test_fp_encrypt_decrypt_data);
	RUN_TEST(test_fp_encrypt_decrypt_key);
	RUN_TEST(test_fp_generate_gsc_session_key);
	RUN_TEST(test_fp_generate_gsc_session_key_fail);
	RUN_TEST(test_fp_decrypt_data_with_gsc_session_key_in_place);
	RUN_TEST(test_fp_decrypt_data_with_gsc_session_key_in_place_fail);
	RUN_TEST(test_fp_encrypt_data_with_ecdh_key_in_place);
	test_print_result();
}
