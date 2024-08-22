/* Copyright 2024 The ChromiumOS Authors
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
#include "rollback.h"

#include <assert.h>
#include <stdbool.h>

#include <zephyr/ztest.h>

ZTEST_SUITE(fpsensor_auth_crypto_stateless, NULL, NULL, NULL, NULL, NULL);

ZTEST(fpsensor_auth_crypto_stateless, test_fp_create_ec_key_from_pubkey)
{
	fp_elliptic_curve_public_key pubkey = {
		.x = {
			0x85, 0xad, 0x35, 0x23, 0x05, 0x1e, 0x33, 0x3f,
			0xca, 0xa7, 0xea, 0xa5, 0x88, 0x33, 0x12, 0x95,
			0xa7, 0xb5, 0x98, 0x9f, 0x32, 0xef, 0x7d, 0xe9,
			0xf8, 0x70, 0x14, 0x5e, 0x89, 0xcb, 0xde, 0x1f,
		},
		.y = {
			0xd1, 0xdc, 0x91, 0xc6, 0xe6, 0x5b, 0x1e, 0x3c,
			0x01, 0x6c, 0xe6, 0x50, 0x25, 0x5d, 0x89, 0xcf,
			0xb7, 0x8d, 0x88, 0xb9, 0x0d, 0x09, 0x41, 0xf1,
			0x09, 0x4f, 0x61, 0x55, 0x6c, 0xc4, 0x96, 0x6b,
		},
	};

	bssl::UniquePtr<EC_KEY> key = create_ec_key_from_pubkey(pubkey);

	zassert_not_equal(key.get(), nullptr);
	zassert_equal(EC_KEY_check_key(key.get()), 1);
}

ZTEST(fpsensor_auth_crypto_stateless, test_fp_create_ec_key_from_pubkey_fail)
{
	fp_elliptic_curve_public_key pubkey = {
		.x = {},
		.y = {},
	};

	bssl::UniquePtr<EC_KEY> key = create_ec_key_from_pubkey(pubkey);

	zassert_equal(key.get(), nullptr);
}

ZTEST(fpsensor_auth_crypto_stateless, test_fp_create_pubkey_from_ec_key)
{
	fp_elliptic_curve_public_key pubkey = {
		.x = {
			0x85, 0xad, 0x35, 0x23, 0x05, 0x1e, 0x33, 0x3f,
			0xca, 0xa7, 0xea, 0xa5, 0x88, 0x33, 0x12, 0x95,
			0xa7, 0xb5, 0x98, 0x9f, 0x32, 0xef, 0x7d, 0xe9,
			0xf8, 0x70, 0x14, 0x5e, 0x89, 0xcb, 0xde, 0x1f,
		},
		.y = {
			0xd1, 0xdc, 0x91, 0xc6, 0xe6, 0x5b, 0x1e, 0x3c,
			0x01, 0x6c, 0xe6, 0x50, 0x25, 0x5d, 0x89, 0xcf,
			0xb7, 0x8d, 0x88, 0xb9, 0x0d, 0x09, 0x41, 0xf1,
			0x09, 0x4f, 0x61, 0x55, 0x6c, 0xc4, 0x96, 0x6b,
		},
	};

	bssl::UniquePtr<EC_KEY> key = create_ec_key_from_pubkey(pubkey);

	zassert_not_equal(key.get(), nullptr);
	zassert_equal(EC_KEY_check_key(key.get()), 1);

	auto result = create_pubkey_from_ec_key(*key);
	zassert_true(result.has_value());

	zassert_mem_equal(result->x, pubkey.x, sizeof(pubkey.x));
	zassert_mem_equal(result->y, pubkey.y, sizeof(pubkey.y));
}

ZTEST(fpsensor_auth_crypto_stateless, test_fp_create_ec_key_from_privkey)
{
	std::array<uint8_t, 32> data = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
					 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
					 2, 3, 4, 5, 6, 7, 8, 9, 1, 2 };

	bssl::UniquePtr<EC_KEY> key =
		create_ec_key_from_privkey(data.data(), data.size());

	zassert_not_equal(key.get(), nullptr);

	/* There is nothing to check for the private key. */
}

ZTEST(fpsensor_auth_crypto_stateless, test_fp_create_ec_key_from_privkey_fail)
{
	std::array<uint8_t, 1> data = {};

	bssl::UniquePtr<EC_KEY> key =
		create_ec_key_from_privkey(data.data(), data.size());

	zassert_equal(key.get(), nullptr);
}

ZTEST(fpsensor_auth_crypto_stateless, test_fp_generate_ecdh_shared_secret)
{
	struct fp_elliptic_curve_public_key pubkey = {
		.x = {
			0x85, 0xad, 0x35, 0x23, 0x05, 0x1e, 0x33, 0x3f,
			0xca, 0xa7, 0xea, 0xa5, 0x88, 0x33, 0x12, 0x95,
			0xa7, 0xb5, 0x98, 0x9f, 0x32, 0xef, 0x7d, 0xe9,
			0xf8, 0x70, 0x14, 0x5e, 0x89, 0xcb, 0xde, 0x1f,
		},
		.y = {
			0xd1, 0xdc, 0x91, 0xc6, 0xe6, 0x5b, 0x1e, 0x3c,
			0x01, 0x6c, 0xe6, 0x50, 0x25, 0x5d, 0x89, 0xcf,
			0xb7, 0x8d, 0x88, 0xb9, 0x0d, 0x09, 0x41, 0xf1,
			0x09, 0x4f, 0x61, 0x55, 0x6c, 0xc4, 0x96, 0x6b,
		},
	};

	bssl::UniquePtr<EC_KEY> public_key = create_ec_key_from_pubkey(pubkey);

	zassert_not_equal(public_key.get(), nullptr);

	std::array<uint8_t, 32> privkey = { 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
					    1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
					    2, 3, 4, 5, 6, 7, 8, 9, 1, 2 };

	bssl::UniquePtr<EC_KEY> private_key =
		create_ec_key_from_privkey(privkey.data(), privkey.size());

	zassert_not_equal(private_key.get(), nullptr);

	std::array<uint8_t, 32> shared_secret;
	zassert_equal(generate_ecdh_shared_secret(*private_key, *public_key,
						  shared_secret.data(),
						  shared_secret.size()),
		      EC_SUCCESS);

	std::array<uint8_t, 32> expected_result = {
		0x46, 0x86, 0xca, 0x75, 0xce, 0xa1, 0xde, 0x23,
		0x48, 0xb3, 0x0b, 0xfc, 0xd7, 0xbe, 0x7a, 0xa0,
		0x33, 0x17, 0x6c, 0x97, 0xc6, 0xa7, 0x70, 0x7c,
		0xd4, 0x2c, 0xfd, 0xc0, 0xba, 0xc1, 0x47, 0x01,
	};

	zassert_mem_equal(shared_secret.data(), expected_result.data(),
			  shared_secret.size());
}

ZTEST(fpsensor_auth_crypto_stateless, test_fp_generate_gsc_session_key)
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

	zassert_equal(generate_gsc_session_key(auth_nonce, gsc_nonce,
					       pairing_key, gsc_session_key),
		      EC_SUCCESS);

	std::array<uint8_t, 32> expected_gsc_session_key = {
		0x1a, 0x1a, 0x3c, 0x33, 0x7f, 0xae, 0xf9, 0x3e,
		0xa8, 0x7c, 0xe4, 0xec, 0xd9, 0xff, 0x45, 0x8a,
		0xb6, 0x2f, 0x75, 0xd5, 0xea, 0x25, 0x93, 0x36,
		0x60, 0xf1, 0xab, 0xd2, 0xf4, 0x9f, 0x22, 0x89,
	};

	zassert_mem_equal(gsc_session_key.data(),
			  expected_gsc_session_key.data(),
			  gsc_session_key.size());
}

ZTEST(fpsensor_auth_crypto_stateless, test_fp_generate_gsc_session_key_fail)
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

	zassert_not_equal(generate_gsc_session_key(auth_nonce, gsc_nonce,
						   pairing_key,
						   gsc_session_key),
			  EC_SUCCESS);
}

ZTEST(fpsensor_auth_crypto_stateless,
      test_fp_decrypt_data_with_gsc_session_key_in_place)
{
	std::array<uint8_t, 32> gsc_session_key = {
		0x1a, 0x1a, 0x3c, 0x33, 0x7f, 0xae, 0xf9, 0x3e,
		0xa8, 0x7c, 0xe4, 0xec, 0xd9, 0xff, 0x45, 0x8a,
		0xb6, 0x2f, 0x75, 0xd5, 0xea, 0x25, 0x93, 0x36,
		0x60, 0xf1, 0xab, 0xd2, 0xf4, 0x9f, 0x22, 0x89,
	};

	std::array<uint8_t, 16> iv = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
	};

	std::array<uint8_t, 32> data = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
					 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
					 2, 3, 4, 5, 6, 7, 8, 9, 1, 2 };

	zassert_equal(decrypt_data_with_gsc_session_key_in_place(
			      gsc_session_key, iv, data),
		      EC_SUCCESS);

	std::array<uint8_t, 32> expected_data = {
		0x6d, 0xed, 0xad, 0x04, 0xf8, 0xdb, 0xae, 0x51,
		0xf8, 0xee, 0x94, 0x7e, 0xdb, 0x12, 0x14, 0x22,
		0x38, 0x32, 0x27, 0xc5, 0x19, 0x72, 0xa3, 0x60,
		0x67, 0x71, 0x25, 0xe8, 0x27, 0x56, 0xc6, 0x35,
	};

	zassert_mem_equal(data.data(), expected_data.data(), data.size());
}

ZTEST(fpsensor_auth_crypto_stateless,
      test_fp_decrypt_data_with_gsc_session_key_in_place_fail)
{
	std::array<uint8_t, 32> gsc_session_key = {
		0x1a, 0x1a, 0x3c, 0x33, 0x7f, 0xae, 0xf9, 0x3e,
		0xa8, 0x7c, 0xe4, 0xec, 0xd9, 0xff, 0x45, 0x8a,
		0xb6, 0x2f, 0x75, 0xd5, 0xea, 0x25, 0x93, 0x36,
		0x60, 0xf1, 0xab, 0xd2, 0xf4, 0x9f, 0x22, 0x89,
	};

	/* Wrong IV size. */
	std::array<uint8_t, 32> iv = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
	};

	std::array<uint8_t, 32> data = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
					 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
					 2, 3, 4, 5, 6, 7, 8, 9, 1, 2 };

	zassert_not_equal(decrypt_data_with_gsc_session_key_in_place(
				  gsc_session_key, iv, data),
			  EC_SUCCESS);
}

ZTEST(fpsensor_auth_crypto_stateless,
      test_fp_encrypt_data_with_ecdh_key_in_place)
{
	constexpr std::array<uint8_t, FP_ELLIPTIC_CURVE_PUBLIC_KEY_IV_LEN>
		zero_iv = { 0 };

	bssl::UniquePtr<EC_KEY> ecdh_key = generate_elliptic_curve_key();

	std::optional<fp_elliptic_curve_public_key> pubkey =
		create_pubkey_from_ec_key(*ecdh_key);

	zassert_true(pubkey.has_value());

	zassert_not_equal(ecdh_key.get(), nullptr);

	struct fp_elliptic_curve_public_key response_pubkey;

	std::array<uint8_t, FP_POSITIVE_MATCH_SECRET_BYTES> secret = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
		6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 1, 2
	};

	std::array<uint8_t, FP_POSITIVE_MATCH_SECRET_BYTES> enc_secret = secret;

	std::array<uint8_t, FP_ELLIPTIC_CURVE_PUBLIC_KEY_IV_LEN> iv = {};

	zassert_mem_equal(iv.data(), zero_iv.data(), iv.size());

	zassert_equal(encrypt_data_with_ecdh_key_in_place(*pubkey, enc_secret,
							  iv, response_pubkey),
		      EC_SUCCESS);

	/* The encrypted data should not be the same as the input. */
	zassert_true(memcmp(enc_secret.data(), secret.data(), secret.size()) !=
		     0);

	/* The IV should not be zero. */
	zassert_true(memcmp(iv.data(), zero_iv.data(), iv.size()) != 0);

	bssl::UniquePtr<EC_KEY> output_key =
		create_ec_key_from_pubkey(response_pubkey);

	zassert_not_equal(output_key.get(), nullptr);

	std::array<uint8_t, 32> share_secret;
	zassert_equal(generate_ecdh_shared_secret(*ecdh_key, *output_key,
						  share_secret.data(),
						  share_secret.size()),
		      EC_SUCCESS);

	AES_KEY aes_key;
	zassert_equal(AES_set_encrypt_key(share_secret.data(), 256, &aes_key),
		      0);

	unsigned int block_num = 0;
	std::array<uint8_t, AES_BLOCK_SIZE> ecount_buf;

	/* The AES CTR uses the same function for encryption & decryption. */
	AES_ctr128_encrypt(enc_secret.data(), enc_secret.data(),
			   enc_secret.size(), &aes_key, iv.data(),
			   ecount_buf.data(), &block_num);

	/* The secret should be the same after decrypt. */
	zassert_mem_equal(enc_secret.data(), secret.data(), secret.size());
}
