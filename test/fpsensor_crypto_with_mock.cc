/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "builtin/assert.h"
#include "common.h"
#include "compile_time_macros.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_crypto.h"
#include "fpsensor/fpsensor_state.h"
#include "mock/fpsensor_crypto_mock.h"
#include "mock/fpsensor_state_mock.h"
#include "mock/rollback_mock.h"
#include "mock/timer_mock.h"
#include "sha256.h"
#include "test_util.h"
#include "util.h"

#include <array>

extern enum ec_error_list
get_ikm(std::span<uint8_t, 64> ikm,
	std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed);

#include <stdbool.h>

static const uint8_t fake_positive_match_salt[] = {
	0x04, 0x1f, 0x5a, 0xac, 0x5f, 0x79, 0x10, 0xaf,
	0x04, 0x1d, 0x46, 0x3a, 0x5f, 0x08, 0xee, 0xcb,
};

static const uint8_t fake_user_id[] = {
	0x28, 0xb5, 0x5a, 0x55, 0x57, 0x1b, 0x26, 0x88, 0xce, 0xc5, 0xd1,
	0xfe, 0x1d, 0x58, 0x5b, 0x94, 0x51, 0xa2, 0x60, 0x49, 0x9f, 0xea,
	0xb1, 0xea, 0xf7, 0x04, 0x2f, 0x0b, 0x20, 0xa5, 0x93, 0x64,
};

/*
 * |expected_positive_match_secret_for_empty_user_id| is obtained by running
 * BoringSSL locally.
 * From https://boringssl.googlesource.com/boringssl
 * commit 365b7a0fcbf273b1fa704d151059e419abd6cfb8
 *
 * Steps to reproduce:
 *
 * Open boringssl/crypto/hkdf/hkdf_test.cc
 * Add the following case to static const HKDFTestVector kTests[]
 *
 * // test positive match secret
 * {
 *   EVP_sha256,
 *   {
 *     // IKM:
 *     // fake_rollback_secret
 *     [ ***Copy 32 octets of fake_rollback_secret here*** ]
 *     // fake_tpm_seed
 *     [ ***Copy 32 octets of fake_tpm_seed here*** ]
 *   }, 64,
 *   {
 *     // fake_positive_match_salt
 *     [ ***Copy 16 octets of fake_positive_match_salt here*** ]
 *   }, 16,
 *   {
 *     // Info:
 *     // "positive_match_secret for user "
 *     0x70, 0x6f, 0x73, 0x69, 0x74, 0x69, 0x76, 0x65,
 *     0x5f, 0x6d, 0x61, 0x74, 0x63, 0x68, 0x5f, 0x73,
 *     0x65, 0x63, 0x72, 0x65, 0x74, 0x20, 0x66, 0x6f,
 *     0x72, 0x20, 0x75, 0x73, 0x65, 0x72, 0x20,
 *     // user_id
 *     [ ***Type 32 octets of 0x00 here*** ]
 *   }, 63,
 *   {  // Expected PRK:
 *     0xc2, 0xff, 0x50, 0x2d, 0xb1, 0x7e, 0x87, 0xb1,
 *     0x25, 0x36, 0x3a, 0x88, 0xe1, 0xdb, 0x4f, 0x98,
 *     0x22, 0xb5, 0x66, 0x8c, 0xab, 0xb7, 0xc7, 0x5e,
 *     0xd7, 0x56, 0xbe, 0xde, 0x82, 0x3f, 0xd0, 0x62,
 *   }, 32,
 *   32, { // 32 = L = FP_POSITIVE_MATCH_SECRET_BYTES
 *     // Expected positive match secret:
 *     [ ***Copy 32 octets of expected positive_match_secret here*** ]
 *   }
 * },
 *
 * Then from boringssl/ execute:
 * mkdir build
 * cd build
 * cmake ..
 * make
 * cd ..
 * go run util/all_tests.go
 */
static const uint8_t expected_positive_match_secret_for_empty_user_id[] = {
	0x8d, 0xc4, 0x5b, 0xdf, 0x55, 0x1e, 0xa8, 0x72, 0xd6, 0xdd, 0xa1,
	0x4c, 0xb8, 0xa1, 0x76, 0x2b, 0xde, 0x38, 0xd5, 0x03, 0xce, 0xe4,
	0x74, 0x51, 0x63, 0x6c, 0x6a, 0x26, 0xa9, 0xb7, 0xfa, 0x68,
};

/*
 * Same as |expected_positive_match_secret_for_empty_user_id| but use
 * |fake_user_id| instead of all-zero user_id.
 */
static const uint8_t expected_positive_match_secret_for_fake_user_id[] = {
	0x0d, 0xf5, 0xac, 0x7c, 0xad, 0x37, 0x0a, 0x66, 0x2f, 0x71, 0xf6,
	0xc6, 0xca, 0x8a, 0x41, 0x69, 0x8a, 0xd3, 0xcf, 0x0b, 0xc4, 0x5a,
	0x5f, 0x4d, 0x54, 0xeb, 0x7b, 0xad, 0x5d, 0x1b, 0xbe, 0x30,
};

test_static int test_get_ikm_failure_seed_not_set(void)
{
	uint8_t ikm[CONFIG_ROLLBACK_SECRET_SIZE + FP_CONTEXT_TPM_BYTES];
	std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed{};

	TEST_ASSERT(get_ikm(ikm, tpm_seed) == EC_ERROR_ACCESS_DENIED);
	return EC_SUCCESS;
}

test_static int test_get_ikm_failure_cannot_get_rollback_secret(void)
{
	uint8_t ikm[CONFIG_ROLLBACK_SECRET_SIZE + FP_CONTEXT_TPM_BYTES];

	/* Given that the TPM seed has been set. */
	TEST_ASSERT(!bytes_are_trivial(default_fake_tpm_seed,
				       sizeof(default_fake_tpm_seed)));

	/* GIVEN that reading the rollback secret will fail. */
	mock_ctrl_rollback.get_secret_fail = true;

	/* THEN get_ikm should fail. */
	TEST_ASSERT(get_ikm(ikm, default_fake_tpm_seed) ==
		    EC_ERROR_HW_INTERNAL);

	/*
	 * Enable get_rollback_secret to succeed before returning from this
	 * test function.
	 */
	mock_ctrl_rollback.get_secret_fail = false;

	return EC_SUCCESS;
}

test_static int test_get_ikm_success(void)
{
	/*
	 * Expected ikm is the concatenation of the rollback secret and the
	 * seed from the TPM.
	 */
	uint8_t ikm[CONFIG_ROLLBACK_SECRET_SIZE + FP_CONTEXT_TPM_BYTES];
	static const uint8_t expected_ikm[] = {
		0xcf, 0xe3, 0x23, 0x76, 0x35, 0x04, 0xc2, 0x0f, 0x0d, 0xb6,
		0x02, 0xa9, 0x68, 0xba, 0x2a, 0x61, 0x86, 0x2a, 0x85, 0xd1,
		0xca, 0x09, 0x54, 0x8a, 0x6b, 0xe2, 0xe3, 0x38, 0xde, 0x5d,
		0x59, 0x14, 0xd9, 0x71, 0xaf, 0xc4, 0xcd, 0x36, 0xe3, 0x60,
		0xf8, 0x5a, 0xa0, 0xa6, 0x2c, 0xb3, 0xf5, 0xe2, 0xeb, 0xb9,
		0xd8, 0x2f, 0xb5, 0x78, 0x5c, 0x79, 0x82, 0xce, 0x06, 0x3f,
		0xcc, 0x23, 0xb9, 0xe7
	};

	/* GIVEN that the TPM seed has been set. */
	TEST_ASSERT(!bytes_are_trivial(default_fake_tpm_seed,
				       sizeof(default_fake_tpm_seed)));

	/* GIVEN that reading the rollback secret will succeed. */
	mock_ctrl_rollback.get_secret_fail = false;

	/* THEN get_ikm will succeed. */
	TEST_ASSERT(get_ikm(ikm, default_fake_tpm_seed) == EC_SUCCESS);
	TEST_ASSERT_ARRAY_EQ(ikm, expected_ikm,
			     CONFIG_ROLLBACK_SECRET_SIZE +
				     FP_CONTEXT_TPM_BYTES);

	return EC_SUCCESS;
}

test_static int test_derive_encryption_key_failure_seed_not_set(void)
{
	FpEncryptionKey unused_key{};
	static const uint8_t unused_salt[FP_CONTEXT_ENCRYPTION_SALT_BYTES] = {
		0
	};
	std::array<uint8_t, FP_CONTEXT_USERID_BYTES> unused_userid{};

	/* GIVEN that the TPM seed is not set. */
	std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed{};

	/* THEN derivation will fail. */
	TEST_ASSERT(derive_encryption_key(unused_key, unused_salt,
					  unused_userid,
					  tpm_seed) == EC_ERROR_ACCESS_DENIED);

	return EC_SUCCESS;
}

static int test_derive_encryption_key_raw(
	std::span<const uint32_t> user_id_, std::span<const uint8_t> salt,
	std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed,
	std::span<const uint8_t> expected_key)
{
	FpEncryptionKey key;
	enum ec_error_list rv;

	rv = derive_encryption_key(
		key, salt,
		{ reinterpret_cast<const uint8_t *>(user_id_.data()),
		  user_id_.size_bytes() },
		tpm_seed);

	TEST_ASSERT(rv == EC_SUCCESS);
	TEST_ASSERT_ARRAY_EQ(key, expected_key, sizeof(key));

	return EC_SUCCESS;
}

test_static int test_derive_encryption_key(void)
{
	/*
	 * These vectors are obtained by choosing the salt and the user_id
	 * (used as "info" in HKDF), and running boringSSL's HKDF
	 * (https://boringssl.googlesource.com/boringssl/+/c0b4c72b6d4c6f4828a373ec454bd646390017d4/crypto/hkdf/)
	 * locally to get the output key. The IKM used in the run is the
	 * concatenation of |fake_rollback_secret| and |fake_tpm_seed|.
	 */
	static const uint32_t user_id1[] = {
		0x608b1b0b, 0xe10d3d24, 0x0bbbe4e6, 0x807b36d9,
		0x2a1f8abc, 0xea38104a, 0x562d9431, 0x64d721c5,
	};

	static const uint8_t salt1[] = {
		0xd0, 0x88, 0x34, 0x15, 0xc0, 0xfa, 0x8e, 0x22,
		0x9f, 0xb4, 0xd5, 0xa9, 0xee, 0xd3, 0x15, 0x19,
	};

	static const uint8_t key1[] = {
		0xdb, 0x49, 0x6e, 0x1b, 0x67, 0x8a, 0x35, 0xc6,
		0xa0, 0x9d, 0xb6, 0xa0, 0x13, 0xf4, 0x21, 0xb3,
	};

	static const uint32_t user_id2[] = {
		0x2546a2ca, 0xf1891f7a, 0x44aad8b8, 0x0d6aac74,
		0x6a4ab846, 0x9c279796, 0x5a72eae1, 0x8276d2a3,
	};

	static const uint8_t salt2[] = {
		0x72, 0x6b, 0xc1, 0xe4, 0x64, 0xd4, 0xff, 0xa2,
		0x5a, 0xac, 0x5b, 0x0b, 0x06, 0x67, 0xe1, 0x53,
	};

	static const uint8_t key2[] = {
		0x8d, 0x53, 0xaf, 0x4c, 0x96, 0xa2, 0xee, 0x46,
		0x9c, 0xe2, 0xe2, 0x6f, 0xe6, 0x66, 0x3d, 0x3a,
	};

	FpEncryptionKey unused_key{};

	static const uint8_t unused_salt[FP_CONTEXT_ENCRYPTION_SALT_BYTES] = {
		0
	};
	static const uint8_t info_wrong_size[] = { 0x01, 0x02, 0x03 };

	/* GIVEN that the TPM seed is set. */
	TEST_ASSERT(!bytes_are_trivial(default_fake_tpm_seed,
				       sizeof(default_fake_tpm_seed)));

	/* GIVEN that reading the rollback secret will succeed. */
	TEST_ASSERT(!mock_ctrl_rollback.get_secret_fail);

	/* THEN the derivation will succeed. */
	TEST_ASSERT(test_derive_encryption_key_raw(user_id1, salt1,
						   default_fake_tpm_seed,
						   key1) == EC_SUCCESS);

	TEST_ASSERT(test_derive_encryption_key_raw(user_id2, salt2,
						   default_fake_tpm_seed,
						   key2) == EC_SUCCESS);

	/* Providing custom info with invalid size should fail. */
	TEST_ASSERT(
		derive_encryption_key(unused_key, unused_salt, info_wrong_size,
				      default_fake_tpm_seed) == EC_ERROR_INVAL);

	return EC_SUCCESS;
}

test_static int test_derive_encryption_key_failure_rollback_fail(void)
{
	FpEncryptionKey unused_key{};
	static const uint8_t unused_salt[FP_CONTEXT_ENCRYPTION_SALT_BYTES] = {
		0
	};
	std::array<uint8_t, FP_CONTEXT_USERID_BYTES> userid{};

	/* GIVEN that reading the rollback secret will fail. */
	mock_ctrl_rollback.get_secret_fail = true;
	/* THEN the derivation will fail. */
	TEST_ASSERT(derive_encryption_key(unused_key, unused_salt, userid,
					  default_fake_tpm_seed) ==
		    EC_ERROR_HW_INTERNAL);

	/* GIVEN that reading the rollback secret will succeed. */
	mock_ctrl_rollback.get_secret_fail = false;
	/* GIVEN that the TPM seed has been set. */
	TEST_ASSERT(!bytes_are_trivial(default_fake_tpm_seed,
				       sizeof(default_fake_tpm_seed)));
	/* THEN the derivation will succeed. */
	TEST_ASSERT(derive_encryption_key(unused_key, unused_salt, userid,
					  default_fake_tpm_seed) == EC_SUCCESS);

	return EC_SUCCESS;
}

test_static int test_derive_positive_match_secret_fail_seed_not_set(void)
{
	static uint8_t output[FP_POSITIVE_MATCH_SECRET_BYTES];
	std::array<uint8_t, FP_CONTEXT_USERID_BYTES> user_id{};

	/* GIVEN that seed is not set. */
	std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed{};
	/* THEN EVEN IF the encryption salt is not trivial. */
	TEST_ASSERT(!bytes_are_trivial(fake_positive_match_salt,
				       sizeof(fake_positive_match_salt)));

	/* Deriving positive match secret will fail. */
	TEST_ASSERT(derive_positive_match_secret(
			    output, fake_positive_match_salt, user_id,
			    tpm_seed) == EC_ERROR_ACCESS_DENIED);

	return EC_SUCCESS;
}

test_static int test_derive_new_pos_match_secret(void)
{
	static uint8_t output[FP_POSITIVE_MATCH_SECRET_BYTES];

	/* First, for empty user_id. */
	std::array<uint8_t, FP_CONTEXT_USERID_BYTES> user_id{};

	/* GIVEN that the encryption salt is not trivial. */
	TEST_ASSERT(!bytes_are_trivial(fake_positive_match_salt,
				       sizeof(fake_positive_match_salt)));
	/*
	 * GIVEN that reading the rollback secret will
	 * succeed.
	 */
	TEST_ASSERT(!mock_ctrl_rollback.get_secret_fail);

	/* GIVEN that the salt is not trivial. */
	TEST_ASSERT(!bytes_are_trivial(fake_positive_match_salt,
				       sizeof(fake_positive_match_salt)));

	/* GIVEN that the TPM seed is set. */
	TEST_ASSERT(!bytes_are_trivial(default_fake_tpm_seed,
				       sizeof(default_fake_tpm_seed)));

	/* THEN the derivation will succeed. */
	TEST_ASSERT(derive_positive_match_secret(
			    output, fake_positive_match_salt, user_id,
			    default_fake_tpm_seed) == EC_SUCCESS);
	TEST_ASSERT_ARRAY_EQ(
		output, expected_positive_match_secret_for_empty_user_id,
		sizeof(expected_positive_match_secret_for_empty_user_id));

	/* Now change the user_id to be non-trivial. */
	memcpy(user_id.data(), fake_user_id, sizeof(fake_user_id));
	TEST_ASSERT(derive_positive_match_secret(
			    output, fake_positive_match_salt, user_id,
			    default_fake_tpm_seed) == EC_SUCCESS);
	TEST_ASSERT_ARRAY_EQ(
		output, expected_positive_match_secret_for_fake_user_id,
		sizeof(expected_positive_match_secret_for_fake_user_id));

	return EC_SUCCESS;
}

test_static int test_derive_positive_match_secret_fail_rollback_fail(void)
{
	static uint8_t output[FP_POSITIVE_MATCH_SECRET_BYTES];
	std::array<uint8_t, FP_CONTEXT_USERID_BYTES> user_id{};

	/* GIVEN that reading secret from anti-rollback block will fail. */
	mock_ctrl_rollback.get_secret_fail = true;
	/* THEN EVEN IF the encryption salt is not trivial. */
	TEST_ASSERT(!bytes_are_trivial(fake_positive_match_salt,
				       sizeof(fake_positive_match_salt)));

	/* Deriving positive match secret will fail. */
	TEST_ASSERT(derive_positive_match_secret(
			    output, fake_positive_match_salt, user_id,
			    default_fake_tpm_seed) == EC_ERROR_HW_INTERNAL);
	mock_ctrl_rollback.get_secret_fail = false;

	return EC_SUCCESS;
}

test_static int test_derive_positive_match_secret_fail_salt_trivial(void)
{
	static uint8_t output[FP_POSITIVE_MATCH_SECRET_BYTES];
	std::array<uint8_t, FP_CONTEXT_USERID_BYTES> user_id{};

	/* GIVEN that the salt is trivial. */
	static const uint8_t salt[FP_CONTEXT_ENCRYPTION_SALT_BYTES] = { 0 };

	/* THEN deriving positive match secret will fail. */
	TEST_ASSERT(derive_positive_match_secret(output, salt, user_id,
						 default_fake_tpm_seed) ==
		    EC_ERROR_INVAL);
	return EC_SUCCESS;
}

test_static int test_derive_positive_match_secret_fail_trivial_key_0x00(void)
{
	static uint8_t output[FP_POSITIVE_MATCH_SECRET_BYTES];
	std::array<uint8_t, FP_CONTEXT_USERID_BYTES> user_id{};

	/* GIVEN that the user ID is set to a known value. */
	memcpy(user_id.data(), fake_user_id, sizeof(fake_user_id));

	/*
	 * GIVEN that reading the rollback secret will succeed.
	 */
	TEST_ASSERT(!mock_ctrl_rollback.get_secret_fail);

	/* GIVEN that the salt is not trivial. */
	TEST_ASSERT(!bytes_are_trivial(fake_positive_match_salt,
				       sizeof(fake_positive_match_salt)));

	/* GIVEN that the sha256 output is trivial (0x00) */
	mock_ctrl_fpsensor_crypto.output_type =
		MOCK_CTRL_FPSENSOR_CRYPTO_HKDF_SHA256_TYPE_ZEROS;

	/* GIVEN that the TPM seed is set. */
	TEST_ASSERT(!bytes_are_trivial(default_fake_tpm_seed,
				       sizeof(default_fake_tpm_seed)));

	/* THEN the derivation will fail with EC_ERROR_HW_INTERNAL. */
	TEST_ASSERT(derive_positive_match_secret(
			    output, fake_positive_match_salt, user_id,
			    default_fake_tpm_seed) == EC_ERROR_HW_INTERNAL);

	/* Now verify success is possible after reverting */

	/* GIVEN that the sha256 output is non-trivial */
	mock_ctrl_fpsensor_crypto.output_type =
		MOCK_CTRL_FPSENSOR_CRYPTO_HKDF_SHA256_TYPE_REAL;

	/* THEN the derivation will succeed */
	TEST_ASSERT(derive_positive_match_secret(
			    output, fake_positive_match_salt, user_id,
			    default_fake_tpm_seed) == EC_SUCCESS);

	/* Clean up any mock changes */
	mock_ctrl_fpsensor_crypto = MOCK_CTRL_DEFAULT_FPSENSOR_CRYPTO;

	return EC_SUCCESS;
}

test_static int test_derive_positive_match_secret_fail_trivial_key_0xff(void)
{
	static uint8_t output[FP_POSITIVE_MATCH_SECRET_BYTES];
	std::array<uint8_t, FP_CONTEXT_USERID_BYTES> user_id{};

	/* GIVEN that the user ID is set to a known value. */
	memcpy(user_id.data(), fake_user_id, sizeof(fake_user_id));

	/*
	 * Given that reading the rollback secret will succeed.
	 */
	TEST_ASSERT(!mock_ctrl_rollback.get_secret_fail);

	/* GIVEN that the salt is not trivial. */
	TEST_ASSERT(!bytes_are_trivial(fake_positive_match_salt,
				       sizeof(fake_positive_match_salt)));

	/* GIVEN that the sha256 output is trivial (0xFF) */
	mock_ctrl_fpsensor_crypto.output_type =
		MOCK_CTRL_FPSENSOR_CRYPTO_HKDF_SHA256_TYPE_FF;

	/* GIVEN that the TPM seed is set. */
	TEST_ASSERT(!bytes_are_trivial(default_fake_tpm_seed,
				       sizeof(default_fake_tpm_seed)));

	/* THEN the derivation will fail with EC_ERROR_HW_INTERNAL. */
	TEST_ASSERT(derive_positive_match_secret(
			    output, fake_positive_match_salt, user_id,
			    default_fake_tpm_seed) == EC_ERROR_HW_INTERNAL);

	/* Now verify success is possible after reverting */

	/* GIVEN that the sha256 output is non-trivial */
	mock_ctrl_fpsensor_crypto.output_type =
		MOCK_CTRL_FPSENSOR_CRYPTO_HKDF_SHA256_TYPE_REAL;

	/* THEN the derivation will succeed */
	TEST_ASSERT(derive_positive_match_secret(
			    output, fake_positive_match_salt, user_id,
			    default_fake_tpm_seed) == EC_SUCCESS);

	/* Clean up any mock changes */
	mock_ctrl_fpsensor_crypto = MOCK_CTRL_DEFAULT_FPSENSOR_CRYPTO;

	return EC_SUCCESS;
}

static int test_enable_positive_match_secret_once(
	struct positive_match_secret_state *dumb_state)
{
	const int8_t kIndexToEnable = 0;
	timestamp_t now = get_time();

	TEST_ASSERT(fp_enable_positive_match_secret(kIndexToEnable,
						    dumb_state) == EC_SUCCESS);
	TEST_ASSERT(dumb_state->template_matched == kIndexToEnable);
	TEST_ASSERT(dumb_state->readable);
	TEST_ASSERT(dumb_state->deadline.val == now.val + (5 * SECOND));

	return EC_SUCCESS;
}

test_static int test_enable_positive_match_secret(void)
{
	struct positive_match_secret_state
		dumb_state = { .template_matched = FP_NO_SUCH_TEMPLATE,
			       .readable = false,
			       .deadline = {
				       .val = 0,
			       } };

	TEST_ASSERT(test_enable_positive_match_secret_once(&dumb_state) ==
		    EC_SUCCESS);

	/* Trying to enable again before reading secret should fail. */
	TEST_ASSERT(fp_enable_positive_match_secret(0, &dumb_state) ==
		    EC_ERROR_UNKNOWN);
	TEST_ASSERT(dumb_state.template_matched == FP_NO_SUCH_TEMPLATE);
	TEST_ASSERT(!dumb_state.readable);
	TEST_ASSERT(dumb_state.deadline.val == 0);

	return EC_SUCCESS;
}

test_static int test_disable_positive_match_secret(void)
{
	struct positive_match_secret_state
		dumb_state = { .template_matched = FP_NO_SUCH_TEMPLATE,
			       .readable = false,
			       .deadline = {
				       .val = 0,
			       } };

	TEST_ASSERT(test_enable_positive_match_secret_once(&dumb_state) ==
		    EC_SUCCESS);

	fp_disable_positive_match_secret(&dumb_state);
	TEST_ASSERT(dumb_state.template_matched == FP_NO_SUCH_TEMPLATE);
	TEST_ASSERT(!dumb_state.readable);
	TEST_ASSERT(dumb_state.deadline.val == 0);

	return EC_SUCCESS;
}

test_static int test_command_read_match_secret(void)
{
	enum ec_status rv;
	struct ec_params_fp_read_match_secret params;
	struct ec_response_fp_read_match_secret resp;
	timestamp_t now = get_time();

	/* For empty user_id. */
	memset(global_context.user_id, 0, sizeof(global_context.user_id));

	/* Invalid finger index should be rejected. */
	params.fgr = FP_NO_SUCH_TEMPLATE;
	rv = test_send_host_command(EC_CMD_FP_READ_MATCH_SECRET, 0, &params,
				    sizeof(params), NULL, 0);
	TEST_ASSERT(rv == EC_RES_INVALID_PARAM);
	params.fgr = FP_MAX_FINGER_COUNT;
	rv = test_send_host_command(EC_CMD_FP_READ_MATCH_SECRET, 0, &params,
				    sizeof(params), NULL, 0);
	TEST_ASSERT(rv == EC_RES_INVALID_PARAM);

	memset(&resp, 0, sizeof(resp));
	/* GIVEN that finger index is valid. */
	params.fgr = 0;

	/* GIVEN that positive match secret is enabled. */
	fp_enable_positive_match_secret(
		params.fgr, &global_context.positive_match_secret_state);

	/* GIVEN that salt is non-trivial. */
	memcpy(global_context.fp_positive_match_salt[0],
	       fake_positive_match_salt,
	       sizeof(global_context.fp_positive_match_salt[0]));
	/* THEN reading positive match secret should succeed. */
	rv = test_send_host_command(EC_CMD_FP_READ_MATCH_SECRET, 0, &params,
				    sizeof(params), &resp, sizeof(resp));
	if (rv != EC_RES_SUCCESS) {
		ccprintf("%s:%s(): rv = %d\n", __FILE__, __func__, rv);
		return -1;
	}
	/* AND the readable bit should be cleared after the read. */
	TEST_ASSERT(global_context.positive_match_secret_state.readable ==
		    false);

	TEST_ASSERT_ARRAY_EQ(
		resp.positive_match_secret,
		expected_positive_match_secret_for_empty_user_id,
		sizeof(expected_positive_match_secret_for_empty_user_id));

	/*
	 * Now try reading secret again.
	 * EVEN IF the deadline has not passed.
	 */
	global_context.positive_match_secret_state.deadline.val =
		now.val + 1 * SECOND;
	rv = test_send_host_command(EC_CMD_FP_READ_MATCH_SECRET, 0, &params,
				    sizeof(params), NULL, 0);
	/*
	 * This time the command should fail because the
	 * fp_pos_match_secret_readable bit is cleared when the secret was read
	 * the first time.
	 */
	TEST_ASSERT(rv == EC_RES_ACCESS_DENIED);

	return EC_SUCCESS;
}

test_static int test_command_read_match_secret_wrong_finger(void)
{
	enum ec_status rv;
	struct ec_params_fp_read_match_secret params;

	/* GIVEN that the finger is not the matched or enrolled finger. */
	params.fgr = 0;
	/*
	 * GIVEN that positive match secret is enabled for a different
	 * finger.
	 */
	fp_enable_positive_match_secret(
		params.fgr + 1, &global_context.positive_match_secret_state);

	/* Reading secret will fail. */
	rv = test_send_host_command(EC_CMD_FP_READ_MATCH_SECRET, 0, &params,
				    sizeof(params), NULL, 0);
	TEST_ASSERT(rv == EC_RES_ACCESS_DENIED);
	return EC_SUCCESS;
}

test_static int test_command_read_match_secret_timeout(void)
{
	enum ec_status rv;
	struct ec_params_fp_read_match_secret params;

	params.fgr = 0;
	/* GIVEN that the read is too late. */
	fp_enable_positive_match_secret(
		params.fgr, &global_context.positive_match_secret_state);
	set_time(global_context.positive_match_secret_state.deadline);

	/* EVEN IF encryption salt is non-trivial. */
	memcpy(global_context.fp_positive_match_salt[0],
	       fake_positive_match_salt,
	       sizeof(global_context.fp_positive_match_salt[0]));
	/* Reading secret will fail. */
	rv = test_send_host_command(EC_CMD_FP_READ_MATCH_SECRET, 0, &params,
				    sizeof(params), NULL, 0);
	TEST_ASSERT(rv == EC_RES_TIMEOUT);
	return EC_SUCCESS;
}

test_static int test_command_read_match_secret_unreadable(void)
{
	enum ec_status rv;
	struct ec_params_fp_read_match_secret params;

	params.fgr = 0;
	/* GIVEN that the readable bit is not set. */
	fp_enable_positive_match_secret(
		params.fgr, &global_context.positive_match_secret_state);
	global_context.positive_match_secret_state.readable = false;

	/* EVEN IF the finger is just matched. */
	TEST_ASSERT(
		global_context.positive_match_secret_state.template_matched ==
		params.fgr);

	/* EVEN IF encryption salt is non-trivial. */
	memcpy(global_context.fp_positive_match_salt[0],
	       fake_positive_match_salt,
	       sizeof(global_context.fp_positive_match_salt[0]));
	/* Reading secret will fail. */
	rv = test_send_host_command(EC_CMD_FP_READ_MATCH_SECRET, 0, &params,
				    sizeof(params), NULL, 0);
	TEST_ASSERT(rv == EC_RES_ACCESS_DENIED);
	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	RUN_TEST(test_derive_encryption_key_failure_seed_not_set);
	RUN_TEST(test_derive_positive_match_secret_fail_seed_not_set);
	RUN_TEST(test_get_ikm_failure_seed_not_set);
	RUN_TEST(test_get_ikm_failure_cannot_get_rollback_secret);
	RUN_TEST(test_get_ikm_success);
	RUN_TEST(test_derive_new_pos_match_secret);
	RUN_TEST(test_derive_positive_match_secret_fail_rollback_fail);
	RUN_TEST(test_derive_positive_match_secret_fail_salt_trivial);
	RUN_TEST(test_derive_positive_match_secret_fail_trivial_key_0x00);
	RUN_TEST(test_derive_positive_match_secret_fail_trivial_key_0xff);
	RUN_TEST(test_derive_encryption_key);
	RUN_TEST(test_derive_encryption_key_failure_rollback_fail);
	/*
	 * Set the TPM seed here because it can only be set once and cannot be
	 * cleared.
	 */
	ASSERT(fpsensor_state_mock_set_tpm_seed(default_fake_tpm_seed) ==
	       EC_SUCCESS);

	/* The following test requires TPM seed to be already set. */
	RUN_TEST(test_enable_positive_match_secret);
	RUN_TEST(test_disable_positive_match_secret);
	RUN_TEST(test_command_read_match_secret);
	RUN_TEST(test_command_read_match_secret_wrong_finger);
	RUN_TEST(test_command_read_match_secret_timeout);
	RUN_TEST(test_command_read_match_secret_unreadable);
	test_print_result();
}
